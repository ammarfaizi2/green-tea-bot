// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 * Copyright (C) 2022 Alviro Iskandar Setiawan <alviro.iskandar@gnuweeb.org>
 */

#include <chrono>
#include <tgvisd/KWorker.hpp>

namespace tgvisd {

using std::chrono::duration;
using namespace std::chrono_literals;

#define	NR_MAX_THPOOL	64
#define NR_MAX_WQ	512

KWorker::KWorker(Main *main):
	main_(main)
{
	uint32_t i;

	wq_     = new struct wq[NR_MAX_WQ];
	thPool_ = new struct thpool[NR_MAX_THPOOL];

	for (i = NR_MAX_THPOOL; i--;) {
		thPool_[i].idx = i;
		thPoolStk_.push(i);
	}

	for (i = NR_MAX_WQ; i--;) {
		wq_[i].idx = i;
		wqStk_.push(i);
	}
}

KWorker::~KWorker(void)
	__acquires(&wqStkLock_)
	__releases(&wqStkLock_)
	__acquires(&pendingWqLock_)
	__releases(&pendingWqLock_)
{
	uint32_t i;

	stop();
	waitForKWorker();
	wqStkLock_.lock();
	pendingWqLock_.lock();

	if (wq_) {
		delete[] wq_;
		wq_ = nullptr;
	}

	if (thPool_) {
		for (i = NR_MAX_THPOOL; i--;) {
			std::thread *t = atomic_load(&thPool_[i].thread);

			if (!t)
				continue;
			t->join();
			delete t;
		}

		delete[] thPool_;
		thPool_ = nullptr;
	}

	wqStkLock_.unlock();
	pendingWqLock_.unlock();
}

void KWorker::waitForKWorker(void)
{
	while (1) {
		uint32_t n = atomic_load(&NrThPoolOnline_);
		if (n == 0)
			break;

		pr_notice("Waiting for %u kworker(s)...", n);
		sleep(1);
	}
}

int KWorker::scheduleWq(wq_f_t func, void *udata, wq_udata_del_f_t fdel)
	__acquires(&wqStkLock_)
	__releases(&wqStkLock_)
	__acquires(&pendingWqLock_)
	__releases(&pendingWqLock_)
{
	struct wq *wq;
	uint32_t idx;
	int ret;

	wqStkLock_.lock();
	ret = -EOWNERDEAD;
	if (shouldStop())
		goto out;
	if (unlikely(!wq_))
		goto out;
	ret = -EAGAIN;
	if (unlikely(wqStk_.empty()))
		goto out;
	idx = wqStk_.top();
	wqStk_.pop();

	wq = &wq_[idx];
	wq->is_done = false;
	wq->func = std::move(func);
	wq->data.kwrk = this;
	wq->data.current = nullptr; /* Filled by the kworker master. */
	wq->data.user_data = udata;
	wq->data.user_data_deleter = std::move(fdel);
	assert(wq->idx == idx);

	pendingWqLock_.lock();
	pendingWq_.push(idx);
	pendingWqLock_.unlock();
	wqCond_.notify_one();
	ret = 0;
out:
	wqStkLock_.unlock();
	return ret;
}

void KWorker::waitForFreeWqSlot(int timeout)
	__acquires(&wqFreeLock_)
	__releases(&wqFreeLock_)
{
	atomic_fetch_add(&nrFreeWqSlotRequests_, 1);
	std::unique_lock lk(wqFreeLock_);
	wqCondFree_.wait_for(lk, std::chrono::milliseconds(timeout));
	atomic_fetch_sub(&nrFreeWqSlotRequests_, 1);
}

void KWorker::putWq(uint32_t idx)
	__acquires(&wqStkLock_)
	__releases(&wqStkLock_)
{
	wqStkLock_.lock();
	wqStk_.push(idx);
	wqStkLock_.unlock();
}

void KWorker::putThPool(uint32_t idx)
	__acquires(&thPoolStkLock_)
	__releases(&thPoolStkLock_)
{
	atomic_store(&thPool_[idx].wq, nullptr);
	thPoolStkLock_.lock();
	thPoolStk_.push(idx);
	thPoolStkLock_.unlock();
}

void KWorker::threadPoolWrk(struct thpool *thpool)
	__acquires(&thpool->mutex)
	__releases(&thpool->mutex)
{
	std::unique_lock<std::mutex> lk(thpool->mutex);
	const uint32_t max_idle_c = 10;
	uint32_t idle_c = 0;
	struct wq *wq;

	atomic_fetch_add(&NrThPoolOnline_, 1);
	atomic_store(&thpool->is_online, true);

loop:
	if (shouldStop())
		goto out;

	wq = atomic_load(&thpool->wq);
	if (!wq) {

		/*
		 * Idle waiting for workqueue.
		 *
		 * If in @max_idle_c seconds we don't get any workqueue,
		 * stop this thread pool.
		 */

		if (idle_c >= max_idle_c)
			goto out;

		auto cret = thpool->cond.wait_for(lk, 1000ms);
		if (cret == std::cv_status::timeout)
			idle_c++;

		goto loop;
	}

	idle_c = 0;
	thpool->setUninterruptible();
	if (!wq->func)
		goto do_put;

	wq->func(&wq->data);
	if (wq->data.user_data && wq->data.user_data_deleter)
		wq->data.user_data_deleter(wq->data.user_data);

	wq->func = nullptr;
	wq->is_done = true;
	wq->data.user_data = nullptr;
	wq->data.user_data_deleter = nullptr;

do_put:
	putWq(wq->idx);
	putThPool(thpool->idx);
	thpool->setInterruptible();
	wqCond_.notify_one();

	/*
	 * Workqueue is/was full and someone is waiting
	 * for the free wq slot. Let them know that we
	 * now have at least one available free wq slot.
	 */
	if (atomic_load(&nrFreeWqSlotRequests_) > 0)
		wqCondFree_.notify_one();

	goto loop;

out:
	atomic_fetch_sub(&NrThPoolOnline_, 1);
	atomic_store(&thpool->is_online, false);
}

struct thpool *KWorker::getThPool(void)
	__acquires(&thPoolStkLock_)
	__releases(&thPoolStkLock_)
{
	struct thpool *ret = nullptr;
	std::thread *t;
	uint32_t idx;

	thPoolStkLock_.lock();
	if (shouldStop())
		goto out;
	if (unlikely(thPool_ == nullptr || thPoolStk_.empty()))
		goto out;
	idx = thPoolStk_.top();
	thPoolStk_.pop();

	ret = &thPool_[idx];
	assert(atomic_load(&ret->wq) == nullptr);
	assert(atomic_load(&ret->thread) == nullptr);

	ret->mutex.lock();
	if (!ret->thpool_is_online()) {
		t = atomic_load(&ret->thread);
		if (t) {
			t->join();
			delete t;
		}

		t = new std::thread([this, ret](void){
			this->threadPoolWrk(ret);
		});
		atomic_store(&ret->thread, t);
	}
	ret->mutex.unlock();
out:
	thPoolStkLock_.unlock();
	return ret;
}

int KWorker::dispatchWq(uint32_t idx)
	__must_hold(&pendingWqLock_)
{
	struct thpool *thpool;
	struct wq *wq;

	if (shouldStop())
		return -EOWNERDEAD;

	thpool = getThPool();
	if (unlikely(!thpool))
		return -EAGAIN;

	assert(thpool->wq == nullptr);

	wq = &wq_[idx];
	wq->data.current = thpool;
	atomic_store(&thpool->wq, wq);
	thpool->cond.notify_one();
	return 0;
}

static void setKWorkerMasterThreadName(void)
{
#if defined(__linux__)
	pthread_setname_np(pthread_self(), "tgvkwrk-master");
#endif
}

void KWorker::run(void)
	__acquires(&pendingWqLock_)
	__releases(&pendingWqLock_)
{
	setKWorkerMasterThreadName();
	std::unique_lock<std::mutex> lk(pendingWqLock_);
loop:
	if (shouldStop())
		return;

	wqCond_.wait_for(lk, 2000ms);
	while (!pendingWq_.empty()) {
		if (dispatchWq(pendingWq_.front()))
			break;
		pendingWq_.pop();
	}
	goto loop;
}

void thpool::__setInterruptible(void)
{
#if defined(__linux__)
	char buf[TASK_COMM_LEN];
	std::thread *t;
	pthread_t pt;

	t = atomic_load(&this->thread);
	if (unlikely(!t))
		return;

	is_interruptible = true;
	pt = t->native_handle();
	snprintf(buf, sizeof(buf), "tgvkwrk-%u", idx);
	pthread_setname_np(pt, buf);
#endif
}

void thpool::__setUninterruptible(void)
{
#if defined(__linux__)
	char buf[TASK_COMM_LEN];
	std::thread *t;
	pthread_t pt;

	t = atomic_load(&this->thread);
	if (unlikely(!t))
		return;

	is_interruptible = false;
	pt = t->native_handle();
	snprintf(buf, sizeof(buf), "tgvkwrk-D-%u", idx);
	pthread_setname_np(pt, buf);
#endif
}

} /* namespace tgvisd */

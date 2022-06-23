// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Ammar Faizi <ammarfaizi2@gnuweeb.org>
 */

#include "WorkQueue.hpp"

#include <chrono>
using namespace std::chrono_literals;

WorkQueue::WorkQueue(uint32_t nr_threads, uint32_t nr_jobs,
		     uint32_t nr_idle_thread) noexcept:
	jobs_queue_(nr_jobs),
	free_job_idx_(nr_jobs),
	nr_threads_(nr_threads),
	nr_jobs_(nr_jobs),
	nr_idle_thread_(nr_idle_thread)
{

	if (nr_idle_thread_ == -1U)
		nr_idle_thread_ = nr_threads_ / 2;
	if (!nr_idle_thread_ || nr_idle_thread_ > nr_threads_)
		nr_idle_thread_ = nr_threads_;

	threads_ = new struct wq_thread[nr_threads];
	jobs_ = new struct wq_job[nr_jobs];
	atomic_store(&nr_sched_idle_, 0);
}

WorkQueue::~WorkQueue(void)
{
	uint32_t i;

	stop_ = true;

	if (helper_thread_) {
		jobs_cond_.notify_all();
		helper_thread_->join();
		delete helper_thread_;
	}

	i = nr_threads_;
	while (i--) {
		std::thread *t;
		jobs_cond_.notify_all();
		t = threads_[i].thread;
		if (t) {
			t->join();
			delete t;
		}
	}

	delete[] threads_;
	delete[] jobs_;
}

void WorkQueue::__wq_thread_worker(struct wq_thread *t,
				   std::unique_lock<std::mutex> &lk) noexcept
	__must_hold(&jobs_lock_)
{
	while (true) {
		struct wq_job *job = get_job();
		if (unlikely(!job))
			return;

		lk.unlock();
		if (job->callback) {
			t->set_task_state(WQ_UNINTERRUPTIBLE);
			job->data.t = t;
			job->callback(&job->data);
			t->set_task_state(WQ_INTERRUPTIBLE);
		}
		lk.lock();
		put_job(job);

		if (atomic_load(&nr_sched_idle_) > 0) {
			lk.unlock();
			sched_idle_cond_.notify_one();
			lk.lock();
		}
	}
}

void WorkQueue::_wq_thread_worker(struct wq_thread *t) noexcept
{
	std::unique_lock<std::mutex> lk(jobs_lock_);
	static const uint32_t max_timout = 300;
	uint32_t timeout_count = 0;

	t->set_task_state(WQ_INTERRUPTIBLE);
	while (!stop_) {
		__wq_thread_worker(t, lk);
		auto cv_ret = jobs_cond_.wait_for(lk, 1s);
		if (unlikely(cv_ret == std::cv_status::timeout)) {
			if (t->idx < nr_idle_thread_)
				continue;
			if (timeout_count++ > max_timout)
				break;
		}
	}
	printf("Thread %u is exiting...\n", t->idx);
}

void WorkQueue::wq_thread_worker(struct wq_thread *t) noexcept
{
	atomic_fetch_add(&nr_on_threads_, 1);
	_wq_thread_worker(t);
	atomic_fetch_sub(&nr_on_threads_, 1);
	t->set_task_state(WQ_ZOMBIE);
}

void WorkQueue::_wq_helper(uint32_t njob)
{
	uint32_t nr_on = atomic_load(&nr_on_threads_);
	uint32_t i;

	if (nr_on == nr_threads_)
		return;

	for (i = nr_idle_thread_; i < nr_threads_; i++) {
		struct wq_thread *t = &threads_[i];
		uint8_t s = atomic_load(&t->state);

		if (s == WQ_ZOMBIE) {
			t->thread->join();
			delete t->thread;
			t->thread = NULL;
			t->set_task_state(WQ_DEAD);
		} else if (t->thread) {
			continue;
		}

		if (njob == 0)
			break;

		njob--;
		t->thread = new std::thread([this, t]{
			printf("Spawning thread %u\n", t->idx);
			this->wq_thread_worker(t);
		});
	}
}

void WorkQueue::wq_helper(void)
{
	std::unique_lock<std::mutex> lk(jobs_lock_, std::defer_lock);

	while (!stop_) {
		uint32_t njob;

		lk.lock();
		njob = jobs_queue_.size();
		if (njob > 0) {
			lk.unlock();
			_wq_helper(njob);
			lk.lock();
		}
		jobs_cond_.wait_for(lk, 10s);
		lk.unlock();
	}
}

static int init_wq_thread(WorkQueue *wq, struct wq_thread *threads,
			  uint32_t nr_threads, uint32_t nr_idle_thread) noexcept
{
	struct wq_thread *t;
	uint32_t i;

	for (i = 0; i < nr_threads; i++) {
		t = &threads[i];
		t->idx = i;
		t->set_task_state(WQ_DEAD);
		t->thread = NULL;
	}

	for (i = 0; i < nr_idle_thread; i++) {
		t = &threads[i];
		t->thread = new std::thread([wq, t]{
			wq->wq_thread_worker(t);
		});
	}
	return 0;
}

static int init_wq_jobs(struct wq_job *jobs, uint32_t nr_jobs,
			wq_stack<uint32_t> &free_job_idx_) noexcept
{
	uint32_t i = nr_jobs;

	while (i--) {
		jobs[i].idx = i;
		jobs[i].data.data = NULL;
		jobs[i].data.t = NULL;
		jobs[i].callback = NULL;
		free_job_idx_.push(i);
	}
	return 0;
}

int WorkQueue::run(void) noexcept
{
	int ret;

	atomic_store(&nr_on_threads_, 0);
	ret = init_wq_jobs(jobs_, nr_jobs_, free_job_idx_);
	if (ret)
		return ret;
	ret = init_wq_thread(this, threads_, nr_threads_, nr_idle_thread_);
	if (ret)
		return ret;

	if (nr_idle_thread_ < nr_threads_) {
		helper_thread_ = new std::thread([this]{
			wq_helper();
		});
	}

	return ret;
}

int64_t WorkQueue::raw_schedule_work(void (*callback)(struct wq_job_data *data),
				     void *data) noexcept
	__must_hold(&jobs_lock_)
{
	struct wq_job *job;
	uint32_t idx;

	if (unlikely(free_job_idx_.empty()))
		return -EAGAIN;

	idx = free_job_idx_.pop();
	job = &jobs_[idx];
	job->data.data = data;
	job->callback = callback;
	jobs_queue_.push(idx);
	return (int64_t)idx;
}

int64_t WorkQueue::try_schedule_work(void (*callback)(struct wq_job_data *data),
				     void *data) noexcept
{
	int64_t idx;
	jobs_lock_.lock();
	idx = raw_schedule_work(callback, data);
	jobs_lock_.unlock();
	if (idx >= 0)
		jobs_cond_.notify_one();
	return idx;
}


int64_t WorkQueue::schedule_work(void (*callback)(struct wq_job_data *data),
				 void *data) noexcept
{
	std::unique_lock<std::mutex> lk(sched_idle_lock_, std::defer_lock);
	bool inc = false;
	int64_t idx;

	while (true) {

		idx = try_schedule_work(callback, data);
		if (idx != -EAGAIN)
			break;

		if (!inc) {
			atomic_fetch_add(&nr_sched_idle_, 1);
			inc = true;
		}

		lk.lock();
		sched_idle_cond_.wait_for(lk, 10s);
		lk.unlock();
	}

	if (inc)
		atomic_fetch_sub(&nr_sched_idle_, 1);

	return idx;
}

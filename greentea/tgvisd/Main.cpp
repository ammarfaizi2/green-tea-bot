// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 * Copyright (C) 2022 Alviro Iskandar Setiawan <alviro.iskandar@gnuweeb.org>
 */

#include <iostream>
#include <cstring>
#include <tgvisd/common.hpp>
#include <tgvisd/Main.hpp>
#include <tgvisd/KWorker.hpp>
// #include <tgvisd/Scraper.hpp>
// #include <tgvisd/Logger/Message.hpp>

#if defined(__linux__)
	#include <signal.h>
	#include <unistd.h>
#endif

namespace tgvisd {

volatile bool stopEventLoop = false;
static void set_interrupt_handler(void);

static void *run_kworker(void *kworker_p)
{
	KWorker *kworker = (KWorker *)kworker_p;

	kworker->run();
	return NULL;
}

void Main::initMySQLConfig(void)
{
	const char *tmp;

	sqlHost_ = getenv("TGVISD_MYSQL_HOST");
	if (unlikely(!sqlHost_))
		throw std::runtime_error("Missing TGVISD_MYSQL_HOST env");

	sqlUser_ = getenv("TGVISD_MYSQL_USER");
	if (unlikely(!sqlUser_))
		throw std::runtime_error("Missing TGVISD_MYSQL_USER env");

	sqlPass_ = getenv("TGVISD_MYSQL_PASS");
	if (unlikely(!sqlPass_))
		throw std::runtime_error("Missing TGVISD_MYSQL_PASS env");

	sqlDBName_ = getenv("TGVISD_MYSQL_DBNAME");
	if (unlikely(!sqlDBName_))
		throw std::runtime_error("Missing TGVISD_MYSQL_DBNAME env");

	tmp = getenv("TGVISD_MYSQL_PORT");
	if (unlikely(!tmp))
		throw std::runtime_error("Missing TGVISD_MYSQL_PORT env");

	sqlPort_ = (uint16_t)atoi(tmp);
}

bool Main::initDbPool(void)
{
	uint32_t i;

	initMySQLConfig();
	dbPool_ = new struct dbpool[NR_DB_POOL];
	if (!dbPool_)
		return false;

	for (i = NR_DB_POOL; i--; ) {
		dbPool_[i].idx = i;
		dbPoolStk_.push(i);
	}
	return true;
}

Main::Main(uint32_t api_id, const char *api_hash, const char *data_path):
	td_(api_id, api_hash, data_path)
{
	set_interrupt_handler();

	if (!initDbPool()) {
		stopEventLoop = false;
		return;
	}

	pr_notice("Spawning kworker thread...");
	kworker_ = new KWorker(this);
	kworkerThread_ = new std::thread(run_kworker, kworker_);

	td_.callback.updateNewMessage = [this](td_api::updateNewMessage &u){
		this->handleUpdateNewMessage(u);
	};
}

void Main::putDbPool(mysql::MySQL *db)
	__acquires(&dbPoolStkLock_)
	__releases(&dbPoolStkLock_)
{
	struct dbpool *dbp;

	dbp = container_of(db, struct dbpool, db);
	dbPoolStkLock_.lock();
	dbPoolStk_.push(dbp->idx);
	dbPoolStkLock_.unlock();
}

mysql::MySQL *Main::getDBPool(void)
	__acquires(&dbPoolStkLock_)
	__releases(&dbPoolStkLock_)
{
	mysql::MySQL *ret;
	uint32_t idx;

	dbPoolStkLock_.lock();
	if (shouldStop())
		goto err;
	if (unlikely(!dbPool_))
		goto err;
	if (unlikely(dbPoolStk_.empty()))
		goto err;
	idx = dbPoolStk_.top();
	dbPoolStk_.pop();
	dbPoolStkLock_.unlock();

	ret = &dbPool_[idx].db;
	if (!ret->getConn()) {
		ret->init(sqlHost_, sqlUser_, sqlPass_, sqlDBName_);
		ret->setPort(sqlPort_);
		ret->connect();
	}
	return ret;

err:
	dbPoolStkLock_.unlock();
	return nullptr;
}

struct updateMsg {
	td_api::object_ptr<td_api::message>	msg;
};

static void wq_handle_update_msg(const struct wq_data *d)
{
	struct updateMsg *u = (struct updateMsg *)d->user_data;

	(void) u;
}

static void wq_free_update_msg(void *udata)
{
	struct updateMsg *u = (struct updateMsg *)udata;
	delete u;
}

void Main::handleUpdateNewMessage(td_api::updateNewMessage &u)
{
	td_api::object_ptr<td_api::message> &msg = u.message_;
	struct updateMsg *m;
	int ret = -EAGAIN;

	/*
	 * Don't be trashing the workqueue with
	 * empty messages. Do an early return here.
	 */
	if (unlikely(!msg))
		return;
	if (unlikely(!msg->content_))
		return;

	m = new struct updateMsg;
	m->msg = std::move(msg);

retry:
	if (shouldStop())
		goto out;

	ret = kworker_->scheduleWq(wq_handle_update_msg, m, wq_free_update_msg);
	if (ret == -EAGAIN) {
		constexpr int timeout_ms = 1000;
		kworker_->waitForFreeWqSlot(timeout_ms);
		goto retry;
	}

out:
	/*
	 * If the scheduleWq() fails, we must delete the @m it here.
	 */
	if (unlikely(ret))
		wq_free_update_msg(m);
}

Main::~Main(void)
{
	td_.setCancelDelayedWork(true);

	if (kworker_)
		kworker_->stop();

	if (kworkerThread_) {
		kworkerThread_->join();
		delete kworkerThread_;
		kworkerThread_ = nullptr;
	}

	if (kworker_) {
		delete kworker_;
		kworker_ = nullptr;
	}

	if (dbPool_) {
		delete[] dbPool_;
		dbPool_ = nullptr;
	}

	td_.close();
#if defined(__linux__)
	pr_notice("Syncing...");
	sync();
#endif
}


__hot int Main::run(void)
{
	constexpr int timeout = 1;

	td_.loop(timeout);
	isReady_ = true;

	while (likely(!stopEventLoop))
		td_.loop(timeout);

	return 0;
}



#if defined(__linux__)
/* 
 * Interrupt handler for Linux only.
 */
static std::mutex sig_mutex;
static volatile bool is_sighandler_set = false;

__cold static void main_sighandler(int sig)
{
	if (!stopEventLoop) {
		printf("\nGot an interrupt signal %d\n", sig);
		stopEventLoop = true;
	}
}

__cold static void set_interrupt_handler(void)
{
	struct sigaction sa;
	int ern;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = main_sighandler;

	/*
	 * Use signal interrupt handler to kill the
	 * process gracefully on Linux.
	 *
	 * Mutex note:
	 * We may have several instances calling
	 * Main::Main simultaneously, so we need a
	 * mutex here.
	 *
	 * Signal handler is only required to be
	 * set once.
	 */
	sig_mutex.lock();
	if (unlikely(!is_sighandler_set)) {
		if (unlikely(sigaction(SIGINT, &sa, NULL) < 0))
			goto err;
		if (unlikely(sigaction(SIGHUP, &sa, NULL) < 0))
			goto err;
		if (unlikely(sigaction(SIGTERM, &sa, NULL) < 0))
			goto err;
		is_sighandler_set = true;
	}
	sig_mutex.unlock();
	return;

err:
	sig_mutex.unlock();
	ern = errno;
	panic("Failed to call sigaction(): (%d) %s", ern, strerror(ern));
}

#else /* #if defined(__linux__) */

static void set_interrupt_handler(void)
{
}

#endif


} /* namespace tgvisd */

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
#include <tgvisd/Logger/Message.hpp>

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

__cold void Main::initMySQLConfig(void)
{
	const char *tmp;

	tmp = getenv("TGVISD_MYSQL_HOST");
	if (unlikely(!tmp)) {
		tmp = "Missing TGVISD_MYSQL_HOST env";
		goto err;
	}
	sqlHost_= tmp;

	tmp = getenv("TGVISD_MYSQL_USER");
	if (unlikely(!tmp)) {
		tmp = "Missing TGVISD_MYSQL_USER env";
		goto err;
	}
	sqlUser_ = tmp;

	tmp = getenv("TGVISD_MYSQL_PASS");
	if (unlikely(!tmp)) {
		tmp = "Missing TGVISD_MYSQL_PASS env";
		goto err;
	}
	sqlPass_ = tmp;

	tmp = getenv("TGVISD_MYSQL_DBNAME");
	if (unlikely(!tmp)) {
		tmp = "Missing TGVISD_MYSQL_DBNAME env";
		goto err;
	}
	sqlDBName_ = tmp;

	tmp = getenv("TGVISD_MYSQL_PORT");
	if (unlikely(!tmp)) {
		tmp = "Missing TGVISD_MYSQL_PORT env";
		goto err;
	}

	sqlPort_ = (uint16_t)atoi(tmp);
	return;

err:
	throw std::runtime_error(tmp);
}

__cold bool Main::initDbPool(void)
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

__cold Main::Main(uint32_t api_id, const char *api_hash, const char *data_path):
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

std::mutex *Main::getChatLock(int64_t tg_chat_id)
	__acquires(&chatLockMapLock_)
	__releases(&chatLockMapLock_)
{
	std::mutex *ret = nullptr;

	if (shouldStop())
		return ret;

	chatLockMapLock_.lock();
	if (unlikely(!chatLockMap_))
		chatLockMap_ = new std::unordered_map<int64_t, std::mutex *>();

	const auto &it = chatLockMap_->find(tg_chat_id);
	if (it == chatLockMap_->end()) {
		ret = new std::mutex;
		chatLockMap_->emplace(tg_chat_id, ret);
		nrChatLockMap_++;
	} else {
		ret = it->second;
	}
	chatLockMapLock_.unlock();
	return ret;
}

std::mutex *Main::getUserLock(uint64_t tg_user_id)
	__acquires(&userLockMapLock_)
	__releases(&userLockMapLock_)
{
	std::mutex *ret = nullptr;

	if (shouldStop())
		return ret;

	userLockMapLock_.lock();
	if (unlikely(!userLockMap_))
		userLockMap_ = new std::unordered_map<uint64_t, std::mutex *>();

	const auto &it = userLockMap_->find(tg_user_id);
	if (it == userLockMap_->end()) {
		ret = new std::mutex;
		userLockMap_->emplace(tg_user_id, ret);
		nrUserLockMap_++;
	} else {
		ret = it->second;
	}
	userLockMapLock_.unlock();
	return ret;
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

	if (shouldStop())
		return nullptr;

	dbPoolStkLock_.lock();
	if (unlikely(!dbPool_ || dbPoolStk_.empty())) {
		dbPoolStkLock_.unlock();
		return nullptr;
	}
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
}

struct updateMsg {
	td_api::object_ptr<td_api::message>	msg;
};

static void wq_handle_update_msg(const struct wq_data *d)
{
	struct updateMsg *u = (struct updateMsg *)d->user_data;

	if (!u->msg)
		return;

	tgvisd::Logger::Message lmsg(*u->msg);
	lmsg.save();
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

__cold Main::~Main(void)
	__acquires(&dbPoolStkLock_)
	__releases(&dbPoolStkLock_)
	__acquires(&chatLockMapLock_)
	__releases(&chatLockMapLock_)
	__acquires(&userLockMapLock_)
	__releases(&userLockMapLock_)

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

	dbPoolStkLock_.lock();
	if (dbPool_) {
		delete[] dbPool_;
		dbPool_ = nullptr;
	}
	dbPoolStkLock_.unlock();

	chatLockMapLock_.lock();
	if (chatLockMap_) {
		for (auto &i: *chatLockMap_) {
			std::mutex *mut;
			mut = i.second;
			mut->lock();
			mut->unlock();
			delete mut;
			i.second = nullptr;
		}
		delete chatLockMap_;
		chatLockMap_ = nullptr;
	}
	chatLockMapLock_.unlock();

	userLockMapLock_.lock();
	if (userLockMap_) {
		for (auto &i: *userLockMap_) {
			std::mutex *mut;
			mut = i.second;
			mut->lock();
			mut->unlock();
			delete mut;
			i.second = nullptr;
		}
		delete userLockMap_;
		userLockMap_ = nullptr;
	}
	userLockMapLock_.unlock();

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

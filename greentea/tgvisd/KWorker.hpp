// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 * Copyright (C) 2022 Alviro Iskandar Setiawan <alviro.iskandar@gnweeb.org>
 */

#ifndef TGVISD__KWORKER_HPP
#define TGVISD__KWORKER_HPP

#if defined(__linux__)
	#include <unistd.h>
	#include <pthread.h>
#endif

#include <condition_variable>
#include <tgvisd/common.hpp>
#include <tgvisd/Main.hpp>
#include <functional>
#include <thread>
#include <atomic>
#include <stack>
#include <queue>
#include <mutex>

#define THPOOL_STACK_SIZE (8192*1024)

namespace tgvisd {

struct wq;
class KWorker;

struct thpool {
public:
	std::condition_variable		cond;
	std::mutex			mutex;
	std::atomic<std::thread *>	thread;
	std::atomic<struct wq *>	wq;
	uint32_t			idx;
	std::atomic<bool>		is_online = false;
	bool				is_interruptible = true;
	void				*ustack;

	inline bool thpool_is_online(void)
	{
		return atomic_load(&is_online);
	}
	void __setInterruptible(void);
	void __setUninterruptible(void);

	inline void setInterruptible(void)
	{
		if (!is_interruptible)
			__setInterruptible();
	}

	inline void setUninterruptible(void)
	{
		if (is_interruptible)
			__setUninterruptible();
	}
};

typedef std::function<void(void *user_data)> wq_udata_del_f_t;

struct wq_data {
	KWorker			*kwrk      = nullptr;
	struct thpool		*current   = nullptr;
	void			*user_data = nullptr;
	wq_udata_del_f_t	user_data_deleter = nullptr;
};

typedef std::function<void(const struct wq_data *data)> wq_f_t;

struct wq {
	bool		is_done = false;
	wq_f_t		func    = nullptr;
	struct wq_data	data;
	uint32_t	idx;
};

class KWorker {

private:
	volatile bool		stop_		= false;
	Main			*main_		= nullptr;
	struct thpool		*thPool_	= nullptr;
	struct wq		*wq_		= nullptr;

	std::mutex		thPoolStkLock_;
	std::stack<uint32_t>	thPoolStk_;
	std::mutex		wqStkLock_;
	std::stack<uint32_t>	wqStk_;
	std::mutex		pendingWqLock_;
	std::queue<uint32_t>	pendingWq_;
	std::condition_variable	wqCond_;
	std::mutex		wqFreeLock_;
	std::condition_variable	wqCondFree_;
	std::atomic<uint32_t>	NrThPoolOnline_ = 0;
	std::atomic<uint32_t>	nrFreeWqSlotRequests_ = 0;
public:
	inline void stop(void)
	{
		stop_ = true;
	}

	inline bool shouldStop(void)
	{
		return unlikely(stop_ || main_->shouldStop());
	}

	KWorker(Main *main);
	~KWorker(void);

	int scheduleWq(wq_f_t func, void *udata = nullptr,
		       wq_udata_del_f_t fdel = nullptr);
	void waitForFreeWqSlot(int timeout);

	void run(void);

private:
	void waitForKWorker(void);
	void putWq(uint32_t idx);
	void putThPool(uint32_t idx);
	int dispatchWq(uint32_t idx);
	struct thpool *getThPool(void);
	void threadPoolWrk(struct thpool *thpool);
};

} /* namespace tgvisd */

#endif /* #ifndef TGVISD__KWORKER_HPP */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Ammar Faizi <ammarfaizi2@gnuweeb.org>
 */

#ifndef WORKQUEUE_HPP
#define WORKQUEUE_HPP

#include <stack>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <functional>
#include <condition_variable>

struct wq_job_data {
	void			*data;
	struct wq_thread	*t;
};

typedef std::function<void(struct wq_job_data *data)> wq_job_callback_t;

struct wq_job {
	uint32_t		idx;
	struct wq_job_data	data;
	wq_job_callback_t	callback;
};

enum wq_thread_state {
	WQ_DEAD = 0,
	WQ_RUNNING = 1,
	WQ_INTERRUPTIBLE = 2,
	WQ_UNINTERRUPTIBLE = 3,
	WQ_ZOMBIE = 4,
};

struct wq_thread {
	uint32_t		idx;
	std::atomic<uint8_t>	state;
	std::thread		*thread;

	inline uint8_t get_task_state(void)
	{
		return atomic_load(&this->state);
	}

	inline void set_task_state(enum wq_thread_state state)
	{
		atomic_store(&this->state, (uint8_t)state);
	}
};

#define __must_hold(LOCK)
#ifndef unlikely
#define unlikely(COND) __builtin_expect(!!(COND), 0)
#endif

template<typename T>
struct wq_stack {
	T	*arr_;
	size_t	pos_;
	size_t	max_;

	inline wq_stack(size_t max):
		pos_(max),
		max_(max)
	{
		arr_ = new T[max];
	}

	inline ~wq_stack(void)
	{
		delete[] arr_;
	}

	inline int64_t push(T val)
	{
		arr_[--pos_] = val;
		return pos_;
	}

	inline T top(void)
	{
		return arr_[pos_];
	}

	inline T pop(void)
	{
		return arr_[pos_++];
	}

	inline bool empty(void)
	{
		return max_ == pos_;
	}
};

template<typename T>
struct wq_queue {
	T	*arr_;
	size_t	front_;
	size_t	rear_;
	size_t	and_;

	inline wq_queue(size_t want_max):
		front_(0),
		rear_(0)
	{
		size_t max = 1;
		while (max < want_max)
			max *= 2;

		and_ = max - 1;
		arr_ = new T[max];
	}

	inline ~wq_queue(void)
	{
		delete[] arr_;
	}

	inline size_t size(void)
	{
		if (rear_ >= front_)
			return rear_ - front_;
		else
			return front_ - rear_;
	}

	inline int64_t push(T val)
	{
		arr_[rear_ & and_] = val;
		return rear_++;
	}

	inline T front(void)
	{
		return arr_[front_ & and_];
	}

	inline T pop(void)
	{
		return arr_[front_++ & and_];
	}
};

#if 0
template<typename T>
using wq_queue = std::queue<T>;
template<typename T>
using wq_stack = std::stack<T>;
#endif

class WorkQueue {
private:
	std::mutex		jobs_lock_;
	std::condition_variable	jobs_cond_;
	wq_queue<uint32_t>	jobs_queue_;
	wq_stack<uint32_t>	free_job_idx_;
	struct wq_job		*jobs_ = nullptr;
	struct wq_thread	*threads_ = nullptr;
	uint32_t		nr_threads_;
	uint32_t		nr_jobs_;
	uint32_t		nr_idle_thread_;
	volatile bool		stop_ = false;

	std::mutex		sched_idle_lock_;
	std::condition_variable	sched_idle_cond_;
	std::atomic<uint32_t>	nr_sched_idle_;
	std::thread		*helper_thread_ = nullptr;

	void _wq_helper(uint32_t njob);
	void wq_helper(void);
	void _wq_thread_worker(struct wq_thread *t) noexcept;
	void __wq_thread_worker(struct wq_thread *t,
				std::unique_lock<std::mutex> &lk) noexcept;

	inline struct wq_job *get_job(void) noexcept
		__must_hold(&jobs_lock_)
	{
		uint32_t idx;

		if (unlikely(jobs_queue_.size() == 0))
			return NULL;

		idx = jobs_queue_.pop();
		return &jobs_[idx];
	}

	inline void put_job(struct wq_job *job) noexcept
		__must_hold(&jobs_lock_)
	{
		job->data.data = NULL;
		job->data.t = NULL;
		job->callback = NULL;
		free_job_idx_.push(job->idx);
	}

public:
	std::atomic<uint32_t>	nr_on_threads_;

	~WorkQueue(void);
	WorkQueue(uint32_t nr_threads = 64, uint32_t nr_jobs = 4096,
		  uint32_t nr_idle_thread = -1U) noexcept;
	int run(void) noexcept;
	void wq_thread_worker(struct wq_thread *t) noexcept;
	int64_t raw_schedule_work(wq_job_callback_t callback, void *data = NULL)
		noexcept;
	int64_t schedule_work(wq_job_callback_t callback, void *data = NULL)
		noexcept;
	int64_t try_schedule_work(wq_job_callback_t callback, void *data = NULL)
		noexcept;

	inline void lock_work(void)
	{
		jobs_lock_.lock();
	}

	inline void unlock_work(void)
	{
		jobs_lock_.unlock();
	}

	inline void notify_wrk_all(void)
	{
		jobs_cond_.notify_all();
	}

	inline void notify_wrk_one(void)
	{
		jobs_cond_.notify_one();
	}
};

#endif /* #ifndef WORKQUEUE_HPP */

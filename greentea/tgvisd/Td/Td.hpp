// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd::Td
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 */

#ifndef TGVISD__TD__TD_HPP
#define TGVISD__TD__TD_HPP

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <condition_variable>
#include <unordered_map>
#include <functional>
#include <memory>
#include <atomic>
#include <chrono>

#ifndef likely
#define likely(EXPR)	__builtin_expect((bool)(EXPR), 1)
#endif

#ifndef unlikely
#define unlikely(EXPR)	__builtin_expect((bool)(EXPR), 0)
#endif

namespace td_api = td::td_api;

template <typename T>
using td_optr = td_api::object_ptr<T>;

using Object = td_optr<td_api::Object>;

namespace tgvisd::Td {

using namespace std::chrono_literals;

using std::condition_variable;
using std::unordered_map;
using std::unique_lock;
using std::shared_ptr;
using std::unique_ptr;
using std::function;
using std::atomic;
using std::string;
using std::mutex;

class Callback
{
public:
	std::function<void(td_api::updateAuthorizationState &update)>
		updateAuthorizationState = nullptr;
	inline void execute(td_api::updateAuthorizationState &update)
	{
		if (updateAuthorizationState)
			updateAuthorizationState(update);
	}

	std::function<void(td_api::updateNewChat &update)>
		updateNewChat = nullptr;
	inline void execute(td_api::updateNewChat &update)
	{
		if (updateNewChat)
			updateNewChat(update);
	}

	std::function<void(td_api::updateChatTitle &update)>
		updateChatTitle = nullptr;
	inline void execute(td_api::updateChatTitle &update)
	{
		if (updateChatTitle)
			updateChatTitle(update);
	}

	std::function<void(td_api::updateUser &update)>
		updateUser = nullptr;
	inline void execute(td_api::updateUser &update)
	{
		if (updateUser)
			updateUser(update);
	}

	std::function<void(td_api::updateNewMessage &update)>
		updateNewMessage = nullptr;
	inline void execute(td_api::updateNewMessage &update)
	{
		if (updateNewMessage)
			updateNewMessage(update);
	}
};

class Td {

	typedef unordered_map<uint64_t, function<void(Object)>> handler_map_t;
	typedef unordered_map<int64_t, td_optr<td_api::user>> user_map_t;
	typedef unordered_map<int64_t, string> chat_title_map_t;

private:
	unique_ptr<td::ClientManager> client_manager_;

	mutex on_auth_update_mutex;
	td_optr<td_api::AuthorizationState> authorization_state_;

	atomic<uint64_t> c_query_id_ = 0;
	atomic<uint64_t> auth_query_id_ = 0;
	atomic<uint32_t> c_loop_ = 0;

	mutex handlers_lock_;
	handler_map_t handlers_;

	mutex chat_title_lock_;
	mutex users_lock_;
	chat_title_map_t *chat_title_ = nullptr;
	user_map_t *users_ = nullptr;

	bool closed_ = false;
	bool need_restart_ = false;
	bool is_authorized_ = false;

	int64_t user_id_ = 0;
	int64_t client_id_ = 0;
	const char *api_hash_;
	const char *data_path_;
	uint32_t api_id_;

	inline uint64_t next_query_id(void)
	{
		return atomic_fetch_add(&c_query_id_, 1);
	}

	void restart(void) noexcept;
	void on_authorization_state_update(void) noexcept;
	void process_response(td::ClientManager::Response response) noexcept;
	void process_update(td_optr<td_api::Object> update) noexcept;
	function<void(Object object)> create_auth_query_handler(void) noexcept;
	void check_authentication_error(Object object) noexcept;
	void reclaim_map(void) noexcept;

public:
	Callback callback;

	inline void set_chat_title(int64_t id, string title)
	{
		chat_title_lock_.lock();
		if (unlikely(!chat_title_)) {
			chat_title_lock_.unlock();
			return;
		}
		(*chat_title_)[id] = title;
		chat_title_lock_.unlock();
	}

	inline void set_user(int64_t id, td_optr<td_api::user> u)
	{
		users_lock_.lock();
		if (unlikely(!users_)) {
			users_lock_.unlock();
			return;
		}
		(*users_)[id] = std::move(u);
		users_lock_.unlock();
	}

	Td(uint32_t api_id, const char *api_hash, const char *data_path)
		noexcept;
	~Td(void);
	uint64_t send_query(td_optr<td_api::Function> f,
			    function<void(Object)> handler) noexcept;
	void loop(int timeout) noexcept;
	void close(void) noexcept;

	template <typename T, typename U>
	td_optr<U> send_query_sync(td_optr<T> method, uint32_t timeout,
				   td_optr<td_api::error> *err = nullptr);
};

template <typename U>
struct sq_sync_data {
	unique_lock<mutex>	lock_;
	mutex			mutex_;
	condition_variable	cond_;
	td_optr<U>		ret_;
	td_optr<td_api::error>	*err_;
	volatile bool		finished_ = false;

	inline sq_sync_data(void):
		lock_(mutex_)
	{
	}
};

template <typename U>
static inline auto query_sync_callback(shared_ptr<sq_sync_data<U>> data)
{
	return [data](td_optr<td_api::Object> obj){
		if (unlikely(!obj))
			return;

		if (unlikely(obj->get_id() == td_api::error::ID)) {
			if (!data->err)
				goto out;
			*data->err = td::move_tl_object_as<td_api::error>(obj);
			goto out;
		}

		if (unlikely(obj->get_id() != U::ID))
			goto out;

		data->ret = td::move_tl_object_as<U>(obj);
	out:
		data->finished_ = true;
		data->cond.notify_one();
	};
}

template <typename U>
static inline td_optr<U> __send_query_sync(shared_ptr<sq_sync_data<U>> data,
					   uint32_t timeout)
{
	uint32_t counter = 0;

	while (true) {
		if (data->finished_ || counter >= timeout)
			break;

		data->cond_.wait_for(data->lock_, 1s);
		counter++;
	}

	return data->ret_;
}

/*
 * T for the method name.
 * U for the return value.
 */
template <typename T, typename U>
td_optr<U> Td::send_query_sync(td_optr<T> method, uint32_t timeout,
			       td_optr<td_api::error> *err)
{
	shared_ptr<sq_sync_data<U>> data;
	td_optr<U> ret;

	data = std::make_shared<sq_sync_data<U>>();
	data->ret_ = nullptr;
	data->err_ = err;
	send_query(std::move(method), query_sync_callback<U>(data));
	return __send_query_sync<U>(std::move(data), timeout);
}

} /* namespace tgvisd::Td */

namespace detail {

template <class... Fs>
struct overload;


template <class F>
struct overload<F>: public F
{
	explicit overload(F f):
		F(f)
	{
	}
};


template <class F, class... Fs>
struct overload<F, Fs...>: public overload<F>, overload<Fs...>
{
	overload(F f, Fs... fs):
		overload<F>(f),
		overload<Fs...>(fs...)
	{
	}

	using overload<F>::operator();
	using overload<Fs...>::operator();
};

}  /* namespace detail */


template <class... F>
auto overloaded(F... f)
{
	return detail::overload<F...>(f...);
}


#endif /* #ifndef TGVISD__TD__TD_HPP */

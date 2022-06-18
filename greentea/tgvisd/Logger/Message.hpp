// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Alviro Iskandar Setiawan <alviro.iskandar@gmail.com>
 * @license GPL-2.0-only
 * @package tgvisd::Logger
 *
 * Copyright (C) 2021  Alviro Iskandar Setiawan <alviro.iskandar@gmail.com>
 */

#ifndef TGVISD__LOGGER__MESSAGE_HPP
#define TGVISD__LOGGER__MESSAGE_HPP

#include <tgvisd/KWorker.hpp>

namespace tgvisd::Logger {

class Message
{
private:
	uint64_t		f_id_;
	uint64_t		f_chat_id_;
	uint64_t		f_sender_id_;
	uint64_t		f_tg_msg_id_;
	uint64_t		f_reply_to_tg_msg_id_;
	const char		*f_msg_type_;

	time_t			created_at_;
	time_t			updated_at_;

	bool			f_has_edited_msg_;
	bool			f_is_forwarded_msg_;
	bool			f_is_deleted_;

	const td_api::message	&msg_;
	uint64_t insertToDb(void);

public:
	Message(const td_api::message &msg);
	void save(void);
};

} /* namespace tgvisd::Logger */

#endif /* #ifndef TGVISD__LOGGER__MESSAGE_HPP */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Alviro Iskandar Setiawan <alviro.iskandar@gmail.com>
 * @license GPL-2.0-only
 * @package tgvisd::Logger
 *
 * Copyright (C) 2021  Alviro Iskandar Setiawan <alviro.iskandar@gmail.com>
 */

#include <tgvisd/Logger/Message.hpp>

namespace tgvisd::Logger {

Message::Message(const td_api::message &msg):
	msg_(msg)
{
}

void Message::save(void)
{
}

} /* namespace tgvisd::Logger */

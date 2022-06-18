// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 */

#ifndef TGVISD__MAIN_HPP
#define TGVISD__MAIN_HPP

#include <thread>
#include <tgvisd/Td/Td.hpp>
#include <tgvisd/common.hpp>
#include <mysql/MySQL.hpp>
#include <unordered_map>
#include <stack>

#define NR_DB_POOL	128

namespace tgvisd {

extern volatile bool stopEventLoop;

class Scraper;

class KWorker;

struct dbpool {
	mysql::MySQL		db;
	uint32_t		idx;
};

class Main
{
private:
	tgvisd::Td::Td		td_;
	volatile bool		isReady_ = false;
	std::thread		*kworkerThread_ = nullptr;
	std::thread		*scraperThread_ = nullptr;
	KWorker			*kworker_ = nullptr;
	Scraper			*scraper_ = nullptr;
	struct dbpool		*dbPool_  = nullptr;
	std::mutex		dbPoolStkLock_;
	std::stack<uint32_t>	dbPoolStk_;

	std::mutex					chatLockMapLock_;
	std::unordered_map<int64_t, std::mutex *>	*chatLockMap_ = nullptr;
	uint32_t					nrChatLockMap_ = 0;

	std::mutex					userLockMapLock_;
	std::unordered_map<uint64_t, std::mutex *>	*userLockMap_ = nullptr;
	uint32_t					nrUserLockMap_ = 0;

	/* For MySQL. */
	const char		*sqlHost_   = nullptr;
	const char		*sqlUser_   = nullptr;
	const char		*sqlPass_   = nullptr;
	const char		*sqlDBName_ = nullptr;
	uint16_t		sqlPort_    = 0u;

	void initMySQLConfig(void);
	bool initDbPool(void);

public:
	std::mutex *getChatLock(int64_t tg_chat_id);
	std::mutex *getUserLock(uint64_t tg_user_id);

	Main(uint32_t api_id, const char *api_hash, const char *data_path);
	~Main(void);
	int run(void);

	inline static bool shouldStop(void)
	{
		return unlikely(stopEventLoop);
	}

	inline KWorker *getKWorker(void)
	{
		return kworker_;
	}

	inline void doStop(void)
	{
		stopEventLoop = true;
	}

	inline bool isReady(void)
	{
		return isReady_;
	}


	inline tgvisd::Td::Td *getTd(void)
	{
		return &td_;
	}

	void handleUpdateNewMessage(td_api::updateNewMessage &u);
	void putDbPool(mysql::MySQL *db);
	mysql::MySQL *getDBPool(void);
};


} /* namespace tgvisd */

#endif /* #ifndef TGVISD__MAIN_HPP */

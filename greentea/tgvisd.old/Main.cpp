// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 */

#include <iostream>
#include <tgvisd/Main.hpp>
#include <tgvisd/KWorker.hpp>
#include <tgvisd/Scraper.hpp>
#include <tgvisd/Logger/Message.hpp>

#if defined(__linux__)
	#include <signal.h>
	#include <unistd.h>
#endif

namespace tgvisd {

volatile bool stopEventLoop = false;
static void set_interrupt_handler(void);


__cold Main::Main(uint32_t api_id, const char *api_hash, const char *data_path):
	td_(api_id, api_hash, data_path)
{
	set_interrupt_handler();

	kworker_ = new KWorker(this);
	scraper_ = new Scraper(this);

	pr_notice("Spawning kworker thread...");
	kworkerThread_ = new std::thread([this]{
		this->kworker_->run();
	});
	KWorker::setMasterThreadName(kworkerThread_);

	pr_notice("Spawning scraper thread...");
	scraperThread_ = new std::thread([this]{
		this->scraper_->run();
	});
	Scraper::setThreadName(scraperThread_);

	td_.callback.updateNewMessage = [this](td_api::updateNewMessage &u){
		std::cout << "Get a new message" << std::endl;
		std::thread tr([this, u = std::move(u)](){
			tgvisd::Logger::Message *msg;
			msg = new tgvisd::Logger::Message(kworker_, *u.message_);
			msg->save();
			delete msg;
		});
		tr.detach();
	};
}


__cold Main::~Main(void)
{
	td_.setCancelDelayedWork(true);

	if (kworker_)
		kworker_->stop();

	if (kworkerThread_) {
		pr_notice("Waiting for kworker thread(s) to exit...");
		kworkerThread_->join();
		delete kworkerThread_;
	}

	if (kworker_)
		delete kworker_;

	if (scraper_) {
		pr_notice("Waiting for scraper thread(s) to exit...");
		scraperThread_->join();
		delete scraperThread_;
	}

	if (scraper_)
		delete scraper_;

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
	int ern;
	struct sigaction sa;

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

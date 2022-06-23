
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <tgvisd/common.h>
#include <tgvisd/Td/Td.hpp>
#include <tgvisd/WorkQueue.hpp>

struct msg_data {
	tgvisd::Td::Td *td_;
	td_api::updateNewMessage up_;
	inline msg_data(tgvisd::Td::Td *td, td_api::updateNewMessage up):
		td_(td),
		up_(std::move(up))
	{
	}
};

static volatile bool g_stop = false;

static void handle_message(struct wq_job_data *d)
{
	msg_data *data = (msg_data *)d->data;

	printf("Got a new message: (tidx = %u)\n", d->t->idx);

	delete data;
}

static int run_daemon(const char *api_id, const char *api_hash,
		      const char *data_path)
{
	uint32_t nr_thread = 32;
	uint32_t nr_thread_idle = 4;
	uint32_t nr_jobs = 4096;

	WorkQueue wq(nr_thread, nr_jobs, nr_thread_idle);
	wq.run();
	tgvisd::Td::Td td(atoi(api_id), api_hash, data_path);

	td.callback.updateNewMessage = [&td, &wq](td_api::updateNewMessage &update){
		msg_data *d = new msg_data(&td, std::move(update));
		wq.schedule_work(handle_message, d);
	};

	while (!g_stop)
		td.loop(3);

	td.close();
	return 0;
}

static void handle_signal(int sig)
{
	(void)sig;
	g_stop = true;
}

int main(void)
{
	const char *api_id, *api_hash, *data_path;

	api_id = getenv("TGVISD_API_ID");
	if (!api_id) {
		puts("Missing TGVISD_API_ID");
		return 1;
	}

	api_hash = getenv("TGVISD_API_HASH");
	if (!api_hash) {
		puts("Missing TGVISD_API_HASH");
		return 1;
	}

	data_path = getenv("TGVISD_DATA_PATH");
	if (!data_path) {
		puts("Missing TGVISD_DATA_PATH");
		return 1;
	}

	signal(SIGINT, handle_signal);

	return run_daemon(api_id, api_hash, data_path);
}

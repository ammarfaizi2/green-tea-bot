
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <tgvisd/common.h>
#include <tgvisd/Td/Td.hpp>
#include <tgvisd/WorkQueue.hpp>

#define NR_WQ_THREADS		64
#define NR_WQ_THREADS_IDLE	4
#define NR_WQ_JOBS		4096

static volatile bool g_stop = false;

class Main;

struct new_msg_data {
	Main *main_;
	td_api::updateNewMessage msg_;

	inline new_msg_data(Main *main, td_api::updateNewMessage msg) noexcept:
		main_(main),
		msg_(std::move(msg))
	{
	}
};

class Main {
private:
	tgvisd::Td::Td		td_;
	WorkQueue		wq_;

	void set_callbacks(void);

public:
	Main(uint32_t api_id, const char *api_hash, const char *data_path)
		noexcept;
	~Main(void) noexcept;
	void run(void) noexcept;
	void handle_new_message(new_msg_data *d);

	inline bool should_stop(void)
	{
		return g_stop;
	}
};

__cold Main::Main(uint32_t api_id, const char *api_hash, const char *data_path)
	noexcept:
	td_(api_id, api_hash, data_path),
	wq_(NR_WQ_THREADS, NR_WQ_JOBS, NR_WQ_THREADS_IDLE)
{
	wq_.run();
	set_callbacks();
}

__cold Main::~Main(void) noexcept
{
}

void Main::handle_new_message(new_msg_data *d)
{
	const auto &msg = d->msg_.message_;
	if (!msg->content_)
		return;

	const auto &content = msg->content_;
	if (content->get_id() != td_api::messageText::ID)
		return;

	const auto &fmt_txt = static_cast<const td_api::messageText &>(*content);
	if (!fmt_txt.text_)
		return;

	const std::string &txt = fmt_txt.text_->text_;
	printf("Got message: \"%s\"\n", txt.c_str());
}

static inline auto create_new_msg_cb(Main *m)
{
	return [m](wq_job_data *d){
		new_msg_data *dd = (new_msg_data *)d->data;
		m->handle_new_message(dd);
		delete dd;
	};
}

__cold void Main::set_callbacks(void)
{
	td_.callback.updateNewMessage = [this](td_api::updateNewMessage &up){
		new_msg_data *d = new new_msg_data(this, std::move(up));
		wq_.schedule_work(create_new_msg_cb(this), d);
	};
}

void Main::run(void) noexcept
{
	while (!should_stop())
		td_.loop(1);

	td_.close();
}

static int run_daemon(const char *api_id, const char *api_hash,
		      const char *data_path)
{
	Main m((uint32_t)atoi(api_id), api_hash, data_path);
	m.run();
	return 0;
}

static void handle_signal(int sig)
{
	g_stop = true;
	printf("Got signal: %d\n", sig);
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

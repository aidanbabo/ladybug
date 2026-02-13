#include "task_runner.hpp"

struct JSTask : public Task {
	JSContext& js;
	std::optional<URL> fetched_from;
	std::string body;

	JSTask(JSContext& js, std::optional<URL> fetched_from, std::string body)
		: Task()
		, js(js)
		, fetched_from(fetched_from)
		, body(body)
	{}

	void run() override {
		js.run(body, fetched_from);
	}

	~JSTask() override = default;
};

AfterXHRTask::AfterXHRTask(JSContext& jsctx, int handle)
	: jsctx(jsctx)
	, handle(handle)
{}

void AfterXHRTask::run() {
	if (response)
		jsctx.dispatch_xhr_onload(*response, handle);
}

TaskRunner::TaskRunner(Tab& tab)
	: m_tab(tab)
	, m_tasks()
	, m_mutex()
	, m_cond_var()
{}

void TaskRunner::schedule(std::unique_ptr<Task> task) {
	m_mutex.lock();
	m_tasks.push(std::move(task));
	m_cond_var.notify_all();
	m_mutex.unlock();
}

void TaskRunner::schedule_js(JSContext& js, std::optional<URL> fetched_from, std::string body) {
	auto task = std::make_unique<JSTask>(js, fetched_from, body);
	schedule(std::move(task));
}

void TaskRunner::run() {
	std::optional<std::unique_ptr<Task>> task;
	{
		std::unique_lock lock(m_mutex);
		if (!m_tasks.empty()) {
			task = std::move(m_tasks.front());
			m_tasks.pop();
		}
	}
	if (task)
		(*task)->run();

	// ???
	{
		std::unique_lock lock(m_mutex);
	}
}

TaskRunner::~TaskRunner() = default;

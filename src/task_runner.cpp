#include "task_runner.hpp"

struct Task {
	virtual void run() = 0;
	virtual ~Task() = default;
};

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
};

TaskRunner::TaskRunner(Tab& tab)
	: m_tab(tab)
	, m_tasks()
	, m_mutex()
	, m_cond_var()
{}

void TaskRunner::schedule_js(JSContext& js, std::optional<URL> fetched_from, std::string body) {
	// NO idea what's going on here. I do not know why we have a condvar at all.
	// m_cond_var.wait /* for what? */
	m_mutex.lock();
	m_tasks.push(std::make_unique<JSTask>(js, fetched_from, body));
	m_cond_var.notify_all();
	m_mutex.unlock();
}

void TaskRunner::run() {
	std::optional<std::unique_ptr<Task>> task;
	// m_cond_var.wait /* for what? */
	m_mutex.lock();
	if (!m_tasks.empty()) {
		task = std::move(m_tasks.front());
		m_tasks.pop();
	}
	m_mutex.unlock();
	if (task)
		(*task)->run();

	// ???
	// m_cond_var.wait /* for what? */
	m_mutex.lock();
	m_mutex.unlock();
}

TaskRunner::~TaskRunner() = default;

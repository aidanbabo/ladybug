#include "task_runner.hpp"
#include <iostream>

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

	~JSTask() override = default;
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
	if (fetched_from) {
		std::cerr << "Got " << *fetched_from << " for fetched_from" << std::endl;
	} else {
		std::cerr << "Got nullopt for fetched_from" << std::endl;
	}
	m_mutex.lock();
	m_tasks.push(std::make_unique<JSTask>(js, fetched_from, body));
	m_cond_var.notify_all();
	m_mutex.unlock();
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
	if (task) {
		(*task)->run();
		auto ff = dynamic_cast<JSTask*>(&**task)->fetched_from;
		if (ff) {
			std::cerr << "Gottttttt " << *ff << " for fetched_from" << std::endl;
		} else {
			std::cerr << "Gottttttt nullopt for fetched_from" << std::endl;
		}
	}

	// ???
	{
		std::unique_lock lock(m_mutex);
	}
}

TaskRunner::~TaskRunner() = default;

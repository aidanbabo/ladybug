#pragma once

#include <condition_variable>
#include <memory>
#include <queue>

#include "url.hpp"
#include "http.hpp"
#include "jscontext.hpp"

class Tab;

struct Task {
	virtual void run() = 0;
	virtual ~Task() = default;
};

struct AfterNetworkTask : public Task {
	std::optional<HttpResponse> response;
	~AfterNetworkTask() override = default;
};

struct AfterXHRTask : public AfterNetworkTask {
	JSContext& jsctx;
	int handle;

	AfterXHRTask(JSContext& jsctx, int handle);
	~AfterXHRTask() override = default;

	void run() override;
};

class TaskRunner {
	Tab& m_tab;
	std::queue<std::unique_ptr<Task>> m_tasks;
	std::mutex m_mutex;
	std::condition_variable m_cond_var;

public:
	TaskRunner(Tab& tab);
	void schedule(std::unique_ptr<Task> task);
	void schedule_js(JSContext& js, std::optional<URL> fetched_from, std::string body);
	void run();
	~TaskRunner();
};

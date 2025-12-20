#include <condition_variable>
#include <memory>
#include <queue>

#include "network.hpp"
#include "jscontext.hpp"

class Tab;

struct Task;

class TaskRunner {
	Tab& m_tab;
	std::queue<std::unique_ptr<Task>> m_tasks;
	std::mutex m_mutex;
	std::condition_variable m_cond_var;

public:
	TaskRunner(Tab& tab);
	void schedule_js(JSContext& js, std::optional<URL> fetched_from, std::string body);
	void run();
	~TaskRunner();
};

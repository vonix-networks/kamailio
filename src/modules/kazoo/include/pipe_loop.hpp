/*
 * pipe_loop.hpp
 *
 *  Created on: Sep 6, 2018
 *      Author: root
 */

#ifndef SRC_MODULES_KAZOO3_INCLUDE_PIPE_LOOP_HPP_
#define SRC_MODULES_KAZOO3_INCLUDE_PIPE_LOOP_HPP_

#include <ev.h>
#include <ev++.h>
#include <mutex>

#include "pipe.hpp"

namespace kz
{

template <typename T>
class PipeWorker :
		public Pipe,
		public ev::dynamic_loop
{

public:
	PipeWorker() :
		ev::dynamic_loop(ev::AUTO)
	{};

	PipeWorker(int cmd_pipes[2]) :
		Pipe(cmd_pipes),
		ev::dynamic_loop(ev::AUTO)
	{};

	PipeWorker(int in, int out) :
		Pipe(in, out),
		ev::dynamic_loop(ev::AUTO)
	{};

	PipeWorker(const PipeWorker& pipe) :
		Pipe(pipe),
		ev::dynamic_loop(ev::AUTO)
	{};

	virtual ~PipeWorker() { }

	virtual void stop()	{ this->stopper.send(); }

	virtual void start()	{ this->run_loop(); }

	bool was_stopped() { return stopped; }

	bool receive(T Data) {
		std::lock_guard<std::mutex> lck (receive_mutex);
		return receiveMsg(&Data, sizeof(T));
	}

protected:

	void run_loop()
	{
		start_ev_handlers();
		OnStart();
		run(0);
		stop_ev_handlers();
		OnStop();
	}

	virtual void OnData(T msg)	= 0;
	virtual void OnStartEvHandlers() {};
	virtual void OnStopEvHandlers() {};
	virtual void OnStart() {};
	virtual void OnStop() {};

	T Read()
	{
		T msg = NULL;
		if (read(in(), &msg, sizeof(T)) != sizeof(T)) {
			msg = NULL;
		};
		return msg;
	}

	std::mutex receive_mutex;

private:

	ev::io data_watcher;
	ev::async stopper;
	ev::sig sigint;
	ev::sig sigkill;
	ev::sig sigterm;

	bool stopped = false;

	void start_ev_handlers()
	{
		stopper.set(raw_loop);
		stopper.set<PipeWorker, &PipeWorker::on_stop>(this);
		stopper.start();

		data_watcher.set(raw_loop);
		data_watcher.set(in(), ev::READ);
		data_watcher.set<PipeWorker, &PipeWorker::on_data>(this);
		data_watcher.start();

		sigint.set<PipeWorker, &PipeWorker::on_terminate_signal>(this);
		sigkill.set<PipeWorker, &PipeWorker::on_terminate_signal>(this);
		sigterm.set<PipeWorker, &PipeWorker::on_terminate_signal>(this);

		sigint.set(raw_loop);
		sigkill.set(raw_loop);
		sigterm.set(raw_loop);

	   	sigkill.start(SIGKILL);
		sigint.start(SIGINT);
	   	sigterm.start(SIGTERM);

		OnStartEvHandlers();
	}

	void stop_ev_handlers()
	{
		OnStopEvHandlers();

		stopper.stop();
		data_watcher.stop();
		sigint.stop();
		sigterm.stop();
		sigkill.stop();
	}

	void on_stop()
	{
		this->stopped = true;
		this->stopper.stop();
		this->break_loop(ev::ALL);
	}

	void on_terminate_signal(ev::sig& signal, int code)
	{
		AMQP_TRACE << "received signal to terminate";
		signal.stop();
		OnTerminate(code);
		signal.loop.break_loop(ev::ALL);
	}

	virtual void OnTerminate(int signal)
	{
	}

	virtual void on_xdata()
	{
		T msg = NULL;
		while(read(in(), &msg, sizeof(T)) == sizeof(T)) {
			if (msg != NULL) {
				OnData(msg);
			}
			msg = NULL;
		}
	};

	virtual void on_data()
	{
		T msg = NULL;
		if (read(in(), &msg, sizeof(T)) == sizeof(T)) {
			if (msg != NULL) {
				OnData(msg);
			}
		}
	};
};

template <typename T>
class PipeThread :
		public Pipe,
		public ev::dynamic_loop
{
	typedef std::thread thread_t;

public:
	PipeThread() :
		ev::dynamic_loop(ev::AUTO)
	{};

	PipeThread(int cmd_pipes[2]) :
		Pipe(cmd_pipes),
		ev::dynamic_loop(ev::AUTO)
	{};

	PipeThread(int in, int out) :
		Pipe(in, out),
		ev::dynamic_loop(ev::AUTO)
	{};

	PipeThread(const PipeThread& pipe) :
		Pipe(pipe),
		ev::dynamic_loop(ev::AUTO)
	{};

	virtual ~PipeThread() { }

	virtual void stop()	{ this->stopper.send(); }

	/*
	virtual void start()
	{
		this->run_loop();
	}
	*/
	virtual void start()
	{
		AMQP_DBG << "starting PIPE thread";
//		this->m_thread = std::thread(&PipeThread::run_loop_in_thread, this);
		this->m_thread = std::thread(&PipeThread::run_loop, this);
	}

	bool was_stopped() { return stopped; }

	bool receive(T Data) {
		std::lock_guard<std::mutex> lck (receive_mutex);
		return receiveMsg(&Data, sizeof(T));
	}

protected:

	void run_loop()
	{
		start_ev_handlers();
		OnStart();
		run(0);
		stop_ev_handlers();
		OnStop();
	}

	virtual void OnData(T msg)	= 0;
	virtual void OnStartEvHandlers() {};
	virtual void OnStopEvHandlers() {};
	virtual void OnStart() {};
	virtual void OnStop() {};

	thread_t m_thread;
	std::mutex receive_mutex;

private:

	ev::io data_watcher;
	ev::async stopper;

	bool stopped = false;

	void start_ev_handlers()
	{
		stopper.set(raw_loop);
		stopper.set<PipeThread, &PipeThread::on_stop>(this);
		stopper.start();

		data_watcher.set(raw_loop);
		data_watcher.set(in(), ev::READ);
		data_watcher.set<PipeThread, &PipeThread::on_data>(this);
		data_watcher.start();

		OnStartEvHandlers();
	}

	void stop_ev_handlers()
	{
		OnStopEvHandlers();

		stopper.stop();
		data_watcher.stop();
	}

	void on_stop()
	{
		this->stopped = true;
		this->stopper.stop();
		this->break_loop(ev::ALL);
        try
        {
    		this->m_thread.join();
        }
        catch (const std::runtime_error &error)
        {
        }
	}


	virtual void on_xdata()
	{
		T msg = NULL;
		while(read(in(), &msg, sizeof(T)) == sizeof(T)) {
			if (msg != NULL) {
				OnData(msg);
			}
			msg = NULL;
		}
	};

	virtual void on_data()
	{
		T msg = NULL;
		if (read(in(), &msg, sizeof(T)) == sizeof(T)) {
			if (msg != NULL) {
				OnData(msg);
			}
		}
	};


};

};

#endif /* SRC_MODULES_KAZOO3_INCLUDE_PIPE_LOOP_HPP_ */

/*
 * pipe.hpp
 *
 *  Created on: Sep 6, 2018
 *      Author: root
 */

#ifndef SRC_MODULES_KAZOO3_INCLUDE_PIPE_HPP_
#define SRC_MODULES_KAZOO3_INCLUDE_PIPE_HPP_

extern "C" {
#include <fcntl.h>
}

namespace kz
{
class Pipe
{
private:
    /**
     *  The two filedescriptors that make up the pipe
     *  @var int[]
     */
	int _flags;
    int _fds[2];

    bool set_flags()
    {
    	int x1 = set_flags(_fds[0]);
    	int x2 = set_flags(_fds[1]);
    	return x1 == 0 && x2 == 0;
    }

    int set_flags(int fd)
    {
    	int flags;

    	flags = fcntl(fd, F_GETFL);
    	if (flags < 0)
    		return flags;
    	flags |= _flags;
    	if (fcntl(fd, F_SETFL, flags) < 0)
    		return -1;

    	return 0;
    }

public:
    /**
     *  Constructor
     */
    Pipe(int flags = O_CLOEXEC | O_NONBLOCK) :
    	_flags(flags)
    {
        // construct the pipe
#ifdef _GNU_SOURCE
//        if (pipe2(_fds, O_CLOEXEC | O_NONBLOCK | O_ASYNC /*_flags*/) == 0) return;
        if (pipe2(_fds, _flags) == 0) return;
#else
        if (
            pipe(_fds) == 0 &&
            set_flags() == 0
        ) return;
#endif

        // something went wrong
        throw std::runtime_error(strerror(errno));
    }

    Pipe(int fds[2], int flags = O_CLOEXEC | O_NONBLOCK) :
    	_flags(flags)
    {
    	_fds[0] = fds[0];
    	_fds[1] = fds[1];
    	set_flags();
    }

    Pipe(int in, int out, int flags = O_NONBLOCK | O_ASYNC) :
    	_flags(flags)
    {
    	_fds[0] = in;
    	_fds[1] = out;
    	set_flags();
    }

    Pipe(const Pipe& pipe) :
    	_flags(pipe._flags)

    {
    	_fds[0] = pipe._fds[0];
    	_fds[1] = pipe._fds[1];
    }

    /**
     *  Destructor
     */
    virtual ~Pipe()
    {
        // close the two filedescriptors
        close(_fds[0]);
        close(_fds[1]);
    }

    /**
     *  Expose the filedescriptors
     *  @return int
     */
    int in() const { return _fds[0]; }
    int out() const { return _fds[1]; }

    bool receiveMsg(void * msg, size_t size)
    {
        return write(out(), msg, size) != -1;
    }
    /**
     *  Notify the pipe, so that the other thread wakes up
     */
    void notify()
    {
        // one byte to send
        char byte = 0;

        // send one byte over the pipe - this will wake up the other thread
        write(_fds[1], &byte, 1);
    }
};
}


#endif /* SRC_MODULES_KAZOO3_INCLUDE_PIPE_HPP_ */

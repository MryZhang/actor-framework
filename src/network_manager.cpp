/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011, 2012                                                   *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation, either version 3 of the License                  *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/


#include <cstdio>
#include <thread>
#include <fcntl.h>
#include <cstdint>
#include <cstring>      // strerror
#include <unistd.h>
#include <iostream>
#include <sys/time.h>
#include <sys/types.h>

#include "cppa/self.hpp"
#include "cppa/scheduler.hpp"
#include "cppa/thread_mapped_actor.hpp"

#include "cppa/intrusive/single_reader_queue.hpp"

#include "cppa/detail/mailman.hpp"
#include "cppa/detail/post_office.hpp"
#include "cppa/detail/network_manager.hpp"

namespace {

using namespace cppa;
using namespace cppa::detail;

struct network_manager_impl : network_manager {

    intrusive::single_reader_queue<mm_message> m_mailman_queue;
    std::thread m_mailman_thread;

    intrusive::single_reader_queue<po_message> m_post_office_queue;
    std::thread m_post_office_thread;

    int pipe_fd[2];

    void start() { // override
        if (pipe(pipe_fd) != 0) {
            CPPA_CRITICAL("cannot create pipe");
        }
        // create actors
        //m_post_office.reset(new thread_mapped_actor);
        //m_mailman.reset(new thread_mapped_actor);
        // store some data in local variables for lambdas
        int pipe_fd0 = pipe_fd[0];
        // start threads
        m_post_office_thread = std::thread([this, pipe_fd0] {
            //scoped_self_setter sss{po_ptr.get()};
            post_office_loop(pipe_fd0, this->m_post_office_queue);
        });
        m_mailman_thread = std::thread([] {
            //scoped_self_setter sss{mm_ptr.get()};
            mailman_loop();
        });
    }

    void stop() { // override
        //m_mailman->enqueue(nullptr, make_any_tuple(atom("DONE")));
        m_mailman_thread.join();
        // wait until mailman is done; post_office closes all sockets
        std::atomic_thread_fence(std::memory_order_seq_cst);
        m_post_office_queue.push_back(new po_message);
        //send_to_post_office(po_message{atom("DONE"), -1, 0});
        m_post_office_thread.join();
        close(pipe_fd[0]);
        close(pipe_fd[1]);
    }

    void send_to_post_office(std::unique_ptr<po_message> msg) {
        m_post_office_queue.push_back(msg.release());
        std::uint32_t dummy = 0;
        if (write(pipe_fd[1], &dummy, sizeof(dummy)) != sizeof(dummy)) {
            CPPA_CRITICAL("cannot write to pipe");
        }
    }

    void send_to_mailman(std::unique_ptr<mm_message> msg) {
        m_mailman_queue.push_back(msg.release());
        //m_mailman->enqueue(nullptr, std::move(msg));
    }

};

} // namespace <anonymous>

namespace cppa { namespace detail {

network_manager::~network_manager() {
}

network_manager* network_manager::create_singleton() {
    return new network_manager_impl;
}

} } // namespace cppa::detail

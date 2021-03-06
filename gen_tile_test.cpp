/*
 *
 * Copyright © 2013 mod_tile contributors
 * Copyright © 2013 Kai Krueger
 * Copyright © 2013 Dane Springmeyer
 *
 *This file is part of renderd, a project to render OpenStreetMap tiles
 *with Mapnik.
 *
 * renderd is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * mod_tile is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>

// https://github.com/philsquared/Catch/wiki/Supplying-your-own-main()
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "gen_tile.h"
#include "render_config.h"
#include "request_queue.h"
#include <syslog.h>
#include <sstream>
#include "string.h"
#include <string>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdlib.h>


std::string get_current_stderr() {
    FILE * input = fopen("stderr.out", "r+");
    std::string log_lines;
    unsigned sz = 1024;
    char buffer[sz];
    while (fgets(buffer, 512, input))
    {
        log_lines += buffer;
    }
    // truncate the file now so future reads
    // only get the new stuff
    FILE * input2 = fopen("stderr.out", "w");
    fclose(input2);
    fclose(input);
    return log_lines;
}

struct item * init_render_request(enum protoCmd type) {
    static int counter;
    struct item * item = (struct item *)malloc(sizeof(struct item));
    bzero(item, sizeof(struct item));
    item->req.ver = PROTO_VER;
    strcpy(item->req.xmlname,"default");
    item->req.cmd = type;
    item->mx = counter++;
    return item;
}

void *addition_thread(void * arg) {
    struct request_queue * queue = (struct request_queue *)arg;
    struct item * item;
    enum protoCmd res;
    unsigned int seed = syscall(SYS_gettid);

    for (int i = 0; i < 10; i++) {
        item = init_render_request(cmdDirty);
        item->mx = rand_r(&seed);
        res = request_queue_add_request(queue, item);
    }
}

void *fetch_thread(void * arg) {
    struct request_queue * queue = (struct request_queue *)arg;
    struct item * item;
    enum protoCmd res;

    for (int i = 0; i < 10; i++) {
        item = request_queue_fetch_request(queue);
    }
}

TEST_CASE( "renderd/queueing", "request queueing") {
    SECTION("renderd/queueing/initialisation", "test the initialisation of the request queue") {
        request_queue * queue = request_queue_init();
        REQUIRE( queue != NULL );
        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request add", "test the addition of a single request") {
        request_queue * queue = request_queue_init();
        struct item * item = init_render_request(cmdRender);

        enum protoCmd res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );

        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request add priority", "test the addition of requests with different priorities") {
        struct item * item;
        enum protoCmd res;
        request_queue * queue = request_queue_init();

        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0 );
        item = init_render_request(cmdRender);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );
        item = init_render_request(cmdRenderPrio);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        item = init_render_request(cmdRenderBulk);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 1 );
        item = init_render_request(cmdDirty);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdNotDone );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 1 );

        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request fetch", "test the fetching of a single request") {
        request_queue * queue = request_queue_init();
        struct item * item = init_render_request(cmdRender);

        request_queue_add_request(queue, item);

        struct item *item2 = request_queue_fetch_request(queue);

        REQUIRE( item == item2 );

        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request fetch priority", "test the fetching of requests with different priorities and their ordering") {
        struct item *item2;

        request_queue * queue = request_queue_init();

        struct item * itemR = init_render_request(cmdRender);
        request_queue_add_request(queue, itemR);
        struct item * itemB = init_render_request(cmdRenderBulk);
        request_queue_add_request(queue, itemB);
        struct item * itemD = init_render_request(cmdDirty);
        request_queue_add_request(queue, itemD);
        struct item * itemRP = init_render_request(cmdRenderPrio);
        request_queue_add_request(queue, itemRP);

        //We should be retrieving items in the order RenderPrio, Render, Dirty, Bulk
        item2 = request_queue_fetch_request(queue);
        INFO("itemRP: " << itemRP);
        INFO("itemR: " << itemR);
        INFO("itemD: " << itemD);
        INFO("itemB: " << itemB);
        //REQUIRE( itemRP == item2 );
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemR == item2 );
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemD == item2 );
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemB == item2 );

        request_queue_close(queue);
        }

    SECTION("renderd/queueing/pending requests", "test if de-duplication of requests work") {
        enum protoCmd res;
        struct item * item;
        request_queue * queue = request_queue_init();

        item = init_render_request(cmdRender);
        item->mx = 0;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );

        item = init_render_request(cmdRender);
        item->mx = 0;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );

        item = init_render_request(cmdRender);
        item->mx = 1;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 2 );

        item = init_render_request(cmdRenderPrio);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );

        item = init_render_request(cmdRenderPrio);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );

        item = init_render_request(cmdDirty);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 0 );

        request_queue_close(queue);
    }

    SECTION("renderd/queueing/overflow requests", "test if requests correctly overflow from one request priority to the next") {
        enum protoCmd res;
        struct item * item;
        request_queue * queue = request_queue_init();
        for (int i = 1; i < (2*REQ_LIMIT + DIRTY_LIMIT + 2); i++) {
                item = init_render_request(cmdRenderPrio);
                res = request_queue_add_request(queue, item);
                INFO("i: " << i);
                INFO("NoPrio: " << request_queue_no_requests_queued(queue, cmdRenderPrio));
                INFO("NoRend: " << request_queue_no_requests_queued(queue, cmdRender));
                INFO("NoDirt: " << request_queue_no_requests_queued(queue, cmdDirty));
                INFO("NoBulk: " << request_queue_no_requests_queued(queue, cmdRenderBulk));
                if (i <= REQ_LIMIT) {
                    REQUIRE( res == cmdIgnore );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == i );
                } else if (i <= (REQ_LIMIT + DIRTY_LIMIT)) {
                    //Requests should overflow into the dirty queue
                    REQUIRE( res == cmdNotDone );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == REQ_LIMIT );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == (i - REQ_LIMIT) );
                } else {
                    //Requests should be dropped alltogether
                    REQUIRE( res == cmdNotDone );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == REQ_LIMIT );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == DIRTY_LIMIT);
                }
        }
        request_queue_close(queue);
    }

    SECTION("renderd/queueing/multithreading request addition", "test if there are any issues with multithreading") {
        pthread_t * addition_threads;
        request_queue * queue;

        for (int j = 0; j < 20; j++) { //As we are looking for race conditions, repeat this test many times
            addition_threads = (pthread_t *)calloc(100,sizeof(pthread_t));
            queue = request_queue_init();
            void *status;

            for (int i = 0; i  < 90; i++) {
                if (pthread_create(&addition_threads[i], NULL, addition_thread,
                        (void *) queue)) {
                    INFO("Failed to create thread");
                    REQUIRE( 1 == 0 );
                }
            }

            for (int i = 0; i < 100; i++) {
                pthread_join(addition_threads[i], &status);
            }

            INFO("Itteration " << j);
            REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 900 );

            request_queue_close(queue);
            free(addition_threads);
        }
    }

    SECTION("renderd/queueing/multithreading request fetch", "test if there are any issues with multithreading") {
            pthread_t * fetch_threads;
            struct request_queue * queue;
            struct item * item;

            for (int j = 0; j < 20; j++) { //As we are looking for race conditions, repeat this test many times
                fetch_threads = (pthread_t *)calloc(100,sizeof(pthread_t));
                queue = request_queue_init();
                void *status;

                for (int i = 0; i < 900; i++) {
                    item = init_render_request(cmdDirty);
                    request_queue_add_request(queue, item);
                }

                REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 900 );


                for (int i = 0; i  < 90; i++) {
                    if (pthread_create(&fetch_threads[i], NULL, fetch_thread,
                            (void *) queue)) {
                        INFO("Failed to create thread");
                        REQUIRE( 1 == 0 );
                    }
                }

                for (int i = 0; i < 100; i++) {
                    pthread_join(fetch_threads[i], &status);
                }

                INFO("Itteration " << j);
                REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 0 );

                request_queue_close(queue);
                free(fetch_threads);
            }
        }

    SECTION("renderd/queueing/multithreading request fetch on empty queue", "test if there are any issues with multithreading") {
        pthread_t * fetch_threads;
        pthread_t * addition_threads;
        struct request_queue * queue;
        struct item * item;

        for (int j = 0; j < 50; j++) { //As we are looking for race conditions, repeat this test many times
            fetch_threads = (pthread_t *)calloc(100,sizeof(pthread_t));
            addition_threads = (pthread_t *)calloc(100,sizeof(pthread_t));
            queue = request_queue_init();
            void *status;

            for (int i = 0; i  < 90; i++) {
                if (pthread_create(&fetch_threads[i], NULL, fetch_thread,
                        (void *) queue)) {
                    INFO("Failed to create thread");
                    REQUIRE( 1 == 0 );
                }
            }

            for (int i = 0; i  < 90; i++) {
                if (pthread_create(&addition_threads[i], NULL, addition_thread,
                        (void *) queue)) {
                    INFO("Failed to create thread");
                    REQUIRE( 1 == 0 );
                }
            }

            for (int i = 0; i < 90; i++) {
                pthread_join(fetch_threads[i], &status);
                pthread_join(addition_threads[i], &status);
            }

            INFO("Itteration " << j);
            REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 0 );

            request_queue_close(queue);
            free(fetch_threads);
            free(addition_threads);
        }
    }


    SECTION("renderd/queueing/clear fd", "Test if the clearing of fd work for queues") {
        struct request_queue * queue = request_queue_init();
        struct item * item;

        item = init_render_request(cmdRender);
        item->fd  = 1;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 2;
        request_queue_add_request(queue, item);
        request_queue_clear_requests_by_fd(queue, 2);
        item = init_render_request(cmdRender);
        item->fd  = 3;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 4;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 5;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 6;
        request_queue_add_request(queue, item);
        request_queue_clear_requests_by_fd(queue, 4);

        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 1);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == FD_INVALID);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 3);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == FD_INVALID);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 5);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 6);

        request_queue_close(queue);
    }
}

TEST_CASE( "renderd", "tile generation" ) {

      SECTION("render_init 1", "should throw nice error if paths are invalid") {
          render_init("doesnotexist","doesnotexist",1);
          std::string log_lines = get_current_stderr();
          int found = log_lines.find("Unable to open font directory: doesnotexist");
          //std::cout << "a: " << log_lines << "\n";
          REQUIRE( found > -1 );
      }

      // we run this test twice to ensure that our stderr reading is working correctlyu
      SECTION("render_init 2", "should throw nice error if paths are invalid") {
          render_init("doesnotexist","doesnotexist",1);
          std::string log_lines = get_current_stderr();
          int found = log_lines.find("Unable to open font directory: doesnotexist");
          //std::cout << "b: " << log_lines << "\n";
          REQUIRE( found > -1 );
      }

      SECTION("renderd startup --help", "should start and show help message") {
          int ret = system("./renderd -h");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 0 );
      }

      SECTION("renderd startup unrecognized option", "should return 1") {
          int ret = system("./renderd --doesnotexit");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 1 );
      }

      SECTION("renderd startup invalid option", "should return 1") {
          int ret = system("./renderd -doesnotexit");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 1 );
      }
}

int main (int argc, char* const argv[])
{
  //std::ios_base::sync_with_stdio(false);
  // start by supressing stderr
  // this avoids noisy test output that is intentionally
  // testing for things that produce stderr and also
  // allows us to catch and read it in these tests to validate
  // the stderr contains the right messages
  // http://stackoverflow.com/questions/13533655/how-to-listen-to-stderr-in-c-c-for-sending-to-callback
  FILE * stream = freopen("stderr.out", "w", stderr);
  //setvbuf(stream, 0, _IOLBF, 0); // No Buffering
  openlog("renderd", LOG_PID | LOG_PERROR, LOG_DAEMON);
  int result = Catch::Main( argc, argv );
  return result;
}


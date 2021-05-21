//
// message_client.hpp
// ~~~~~~~~~~~~~~~

#include <iostream>
#include <chrono>
#include "enclave_u.h"
#include "sgx_urts.h"

#include <ctime>
#include <asio.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <deque>
#include <list>
#include <memory>
#include <set>
#include <utility>
//#include "asio.hpp"


#define USE_TEST
#define ROUND 30
#define HALT_BEFORE_INFER 1
#define PRINT_ROUND_INFO

#define SGX_INPUT_MAX (1024U * 1024U * 2U)
#define SGX_OUTPUT_MAX (1024U * 1024U * 2U)

extern uint8_t sgx_input[SGX_INPUT_MAX];
extern uint8_t sgx_output[SGX_OUTPUT_MAX];
extern uint32_t round_counter;

size_t do_infer(size_t input_size, const void *input,
                size_t output_max_size, void *output);

int do_test_net(char ip[], char out_port[]);
int do_net(int in_port, char ip[], char out_port[]);
int do_end_net(int in_port);

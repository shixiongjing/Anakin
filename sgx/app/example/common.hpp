//
// message_client.hpp
// ~~~~~~~~~~~~~~~



#define USE_TEST
#define ROUND 30
#define HALT_BEFORE_INFER 1

#define SGX_INPUT_MAX (1024U * 1024U * 2U)
#define SGX_OUTPUT_MAX (1024U * 1024U * 2U)

uint8_t sgx_input[SGX_INPUT_MAX];
uint8_t sgx_output[SGX_OUTPUT_MAX];

size_t do_infer(size_t input_size, const void *input,
                size_t output_max_size, void *output);

int do_test_net(std::string ip, int port);
int do_net(int in_port, std::string ip, int out_port);
int do_end_net(int in_port, std::string ip, int out_port);
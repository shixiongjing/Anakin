#include <iostream>
#include <chrono>
#include "enclave_u.h"
#include "sgx_urts.h"

#include <ctime>
#include <CLI11.hpp>
#include <asio.hpp>

#include <sys/types.h>
#include <thread>
//#define PRINTINFTIME 1
//#define FORKTEST
#define THREAD_NUM 2
//#include <sgx_tprotected_fs.h>
sgx_enclave_id_t global_eid = 0;
using namespace std;

extern "C" int initialize_enclave(sgx_enclave_id_t *eid, const char *token_path, const char *enclave_name);

#define SGX_INPUT_MAX (1024U * 1024U * 2U)
uint8_t sgx_input[SGX_INPUT_MAX];

#define SGX_OUTPUT_MAX (1024U * 1024U * 2U)
uint8_t sgx_output[SGX_OUTPUT_MAX];
std::chrono::duration<double> inf_time = std::chrono::duration<double>::zero(); 


size_t do_infer(size_t input_size, const void *input,
                size_t output_max_size, void *output) {
    int ecall_retcode = -1;
    size_t result_size = 0;

    auto begin = std::chrono::steady_clock::now();
    #ifdef PRINTINFTIME
    std::cout << "start time: " <<
    std::chrono::duration_cast<std::chrono::milliseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    #endif
    int status = infer(global_eid, &ecall_retcode, input_size, input,
                       output_max_size, output, &result_size);

    if (status != SGX_SUCCESS) {
        std::cerr << "error " << status
                  << ": SGX ecall 'infer' failed" << std::endl;
        exit(1);
    } else if (ecall_retcode) {
        std::cerr << "error " << ecall_retcode
                  << ": invalid inference parameters" << std::endl;
        exit(1);
    }

    const auto end = std::chrono::steady_clock::now();
    #ifdef PRINTINFTIME
    std::cout << "end time: " <<
    std::chrono::duration_cast<std::chrono::milliseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    #endif



    std::chrono::duration<double> elasped_sec = end - begin;
    inf_time += elasped_sec;
    //std::cout << elasped_sec.count()
              //<< " seconds elapsed during only inference" << std::endl;

    return result_size;
}

void thread_infer(){
    for(int i=0;i<30;i++){
        do_infer(0, nullptr, sizeof(sgx_output), sgx_output);
    }
}

void do_test() {
    thread trd[THREAD_NUM];
    for (int i = 0; i< THREAD_NUM; i++)
    {
        trd[i] = thread(thread_infer);
    }
    for (int i = 0; i < THREAD_NUM; i++)
    {
        trd[i].join();
    }
    //do_infer(0, nullptr, sizeof(sgx_output), sgx_output);

#if ENABLE_DEBUG
    auto f = reinterpret_cast<float *>(sgx_output);
    auto n = result_size / sizeof(float);
    for (int i = 0; i < n; ++i) {
        std::cout << f[i] << std::endl;
    }
#endif
}

void do_test_o() {
    size_t output_size = do_infer(0, nullptr, sizeof(sgx_output), sgx_output);
    FILE *output_file = fopen("temp_result.out", "wb");
    if (output_size != fwrite(sgx_output, 1, output_size, output_file)) {
        std::cerr << "error: cannot write output file" << std::endl;
        exit(1);
    }
    fclose(output_file);

#if ENABLE_DEBUG
    auto f = reinterpret_cast<float *>(sgx_output);
    auto n = result_size / sizeof(float);
    for (int i = 0; i < n; ++i) {
        std::cout << f[i] << std::endl;
    }
#endif
}

void do_local(const std::string &input_path, const std::string &output_path) {
    FILE *input_file = fopen(input_path.c_str(), "rb");

    if (!input_file) {
        std::cerr << "error: cannot open input file " << input_path << std::endl;
        exit(1);
    }

    fseek(input_file, 0, SEEK_END);
    long int fend = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);

    if (fend > sizeof(sgx_input)) {
        std::cerr << "error: oversized input" << std::endl;
        exit(1);
    }

    if (fend <= 0) {
        std::cerr << "error: cannot read input file" << std::endl;
        exit(1);
    }

    size_t input_size = fend;
    if (input_size != fread(sgx_input, 1, input_size, input_file)) {
        std::cerr << "error: cannot read input file" << std::endl;
        exit(1);
    }

    fclose(input_file);

    size_t output_size = do_infer(input_size, sgx_input, sizeof(sgx_output), sgx_output);
    FILE *output_file = fopen(output_path.c_str(), "wb");
    if (output_size != fwrite(sgx_output, 1, output_size, output_file)) {
        std::cerr << "error: cannot write output file" << std::endl;
        exit(1);
    }
    fclose(output_file);
}

void do_net(const std::string &outgoing_ip, int port) {
    using asio::ip::tcp;

    asio::io_service io_service;
    asio::streambuf input_buf;
    asio::error_code error;

    while (true) {
        input_buf.consume(input_buf.size());

        try {
            tcp::acceptor acceptor(io_service,
                                   tcp::endpoint(tcp::v4(), port));

            tcp::socket socket(io_service);
            acceptor.accept(socket);

            asio::read(socket, input_buf, error);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            exit(1);
        }
        std::cout << "received data, start infer..." << std::endl;
        size_t result_size =
            do_infer(input_buf.size(),
                     asio::buffer_cast<const char *>(input_buf.data()),
                     sizeof(sgx_output), sgx_output);

        tcp::resolver resolver(io_service);
        tcp::resolver::query query(outgoing_ip, std::to_string(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        std::cout << "end infer, start sending..." << std::endl;
        try {
            tcp::socket socket(io_service);
            asio::connect(socket, endpoint_iterator);
            socket.write_some(asio::buffer(sgx_output, result_size));
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            exit(1);
        }
    }
}

void do_test_net(const std::string &outgoing_ip, int port) {
    using asio::ip::tcp;

    asio::io_service io_service;
    asio::streambuf input_buf;
    asio::error_code error;



        tcp::resolver resolver(io_service);
        tcp::resolver::query query(outgoing_ip, std::to_string(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

        try {
            tcp::socket socket(io_service);
            asio::connect(socket, endpoint_iterator);
            for(int i=0;i<1;i++){
	        size_t result_size = do_infer(0, nullptr, sizeof(sgx_output), sgx_output);
		std::cout << "infer finish, start sending..." << std::endl;
                socket.write_some(asio::buffer(sgx_output, result_size));
            }
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            exit(1);
        }
}

void do_end_net(const std::string &outgoing_ip, int port) {
    using asio::ip::tcp;

    asio::io_service io_service;
    asio::streambuf input_buf;
    asio::error_code error;

    while (true) {
        input_buf.consume(input_buf.size());

        try {
            tcp::acceptor acceptor(io_service,
                                   tcp::endpoint(tcp::v4(), port));

            tcp::socket socket(io_service);
            acceptor.accept(socket);

            asio::read(socket, input_buf, error);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            exit(1);
        }
        std::cout << "received data, start infer..." << std::endl;
        size_t result_size =
            do_infer(input_buf.size(),
                     asio::buffer_cast<const char *>(input_buf.data()),
                     sizeof(sgx_output), sgx_output);
        std::cout << "end infer..." << std::endl;
        
    }
}

int main(int argc, char const *argv[]) {
    CLI::App app{"Anakin inference interface for SGX"};

    std::string arg_model_path;
    app.add_option("model", arg_model_path)->required();
    app.require_subcommand(1);

    CLI::App *subcmd_test = app.add_subcommand("test");

    std::string arg_ifile;
    std::string arg_ofile;
    CLI::App *subcmd_local = app.add_subcommand("local");
    subcmd_local->add_option("input_file", arg_ifile)->required();
    subcmd_local->add_option("output_file", arg_ofile)->required();

    std::string arg_oip;
    int arg_port = 8091;
    CLI::App *subcmd_net = app.add_subcommand("net");
    subcmd_net->add_option("outgoing_ip", arg_oip)->required();
    subcmd_net->add_option("port", arg_port);

    CLI11_PARSE(app, argc, argv);

    #ifdef FORKTEST
    if(fork()==0){
        global_eid = 1;
        std::cout << "pc " << global_eid << " start."  << std::endl;
    }
    else{
        global_eid = 2;
        std::cout << "pc " << global_eid << " start."  << std::endl;
    }
    #endif

    if (initialize_enclave(&global_eid, "anakin_enclave.token", "anakin_enclave.signed") < 0) {
        std::cerr << "error: fail to initialize enclave." << std::endl;
        return 1;
    }

    int ecall_retcode = -1;
    sgx_status_t status = setup_model(global_eid, &ecall_retcode, argv[1]);

    if (status != SGX_SUCCESS) {
        std::cerr << "error: SGX ecall 'setup_model' failed." << std::endl;
        return 1;
    }

    if (ecall_retcode) {
        std::cerr << "error: invalid anakin model." << std::endl;
        return 1;
    }

    std::cout << "model ready" << std::endl;
    auto begin = std::chrono::steady_clock::now();

    std::cout << global_eid << " pc infer start time: " <<
    std::chrono::duration_cast<std::chrono::milliseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;

    if (app.got_subcommand(subcmd_test)) {
        for(int i=0;i<1;i++){
	    do_test();
	    //do_test_net("130.203.153.40", 12345);
	}
    } else if (app.got_subcommand(subcmd_local)) {
        for(int i=0;i<1;i++){
	do_local(arg_ifile, arg_ofile);
	}
    } else if (app.got_subcommand(subcmd_net)) {
        do_end_net(arg_oip, arg_port);
    } else {
        abort();
    }


    std::cout << global_eid << " pc infer end time: " <<
    std::chrono::duration_cast<std::chrono::milliseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;



    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elasped_sec = end - begin;
    std::cout << elasped_sec.count()
              << " seconds elapsed during inference + data loading/decryption" << std::endl;

    std::cout << inf_time.count()
              << " seconds elapsed during only inference" << std::endl;

    return 0;
}

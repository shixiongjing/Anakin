//
// message_client.hpp
// ~~~~~~~~~~~~~~~



#include "common.hpp"

using asio::ip::tcp;



int do_test_net(std::string ip, int port)
{
  
  try
  {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(ip, port);
    message_client c(io_context, endpoints);
    std::thread t([&io_context](){ io_context.run(); });
  
    for(int i=0;i<ROUND;i++)
    {
      size_t result_size =
              do_infer(0, nullptr,
                       sizeof(sgx_output), sgx_output);

      data_message msg;
      msg.body_length(result_size);
      std::memcpy(msg.body(), sgx_output, msg.body_length());
      msg.encode_header();
      c.write(msg);
    }
    
    
    std::cout << '\n' << "Press a key to continue...";
    std::cin.get();
    c.close();
    t.join();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

int do_net(int in_port, std::string ip, int out_port)
{
  try{
    asio::io_context io_context;

    tcp::endpoint endpoint_in(tcp::v4(), in_port);
    tcp::resolver resolver(io_context);
    auto endpoint_out = resolver.resolve(ip, port);
    middle_server server(io_context, endpoint_in, endpoint_out, 1);

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }
}

int do_end_net(int in_port, std::string ip, int out_port)
{
  try{
    asio::io_context io_context;

    tcp::endpoint endpoint_in(tcp::v4(), in_port);
    tcp::resolver resolver(io_context);
    auto endpoint_out = resolver.resolve(ip, port);
    middle_server server(io_context, endpoint_in, endpoint_out, 0);

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }
}

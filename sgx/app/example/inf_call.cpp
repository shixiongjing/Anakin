//
// message_client.hpp
// ~~~~~~~~~~~~~~~



#include "data_message.hpp"

using asio::ip::tcp;
uint32_t round_counter = 0;


int do_test_net(char ip[], char out_port[])
{
  
  try
  {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(ip, out_port);
    message_client c(io_context, endpoints);
    std::thread t([&io_context](){ io_context.run(); });
  
    for(int i=0;i<ROUND;i++)
    {
      size_t result_size = 0;
      result_size = do_infer(0, nullptr, sizeof(sgx_output), sgx_output);

      round_counter++;
      #ifdef PRINT_ROUND_INFO
      std::cout << "round: " << round_counter <<
      "time: " << std::chrono::duration_cast<std::chrono::milliseconds>
      (std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
      #endif

      data_message msg;
      msg.data_buf.assign(sgx_output, sgx_output + result_size);
      msg.header.size = msg.data_buf.size();
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

int do_net(int in_port, char ip[], char out_port[])
{
  try{
    asio::io_context io_context;

    tcp::endpoint endpoint_in(tcp::v4(), in_port);
    tcp::resolver resolver(io_context);
    auto endpoint_out = resolver.resolve(ip, out_port);
    middle_server server(io_context, endpoint_in, endpoint_out, 1);

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }
}

int do_end_net(int in_port)
{
  try{
    asio::io_context io_context;

    tcp::endpoint endpoint_in(tcp::v4(), in_port);
    tcp::resolver resolver(io_context);
    auto endpoint_out = resolver.resolve("1.1.1.1", "60006");
    middle_server server(io_context, endpoint_in, endpoint_out, 0);

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }
}

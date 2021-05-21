//
// data_message.hpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2020 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//



#ifndef data_message_HPP
#define data_message_HPP


#include "common.hpp"

using asio::ip::tcp;

class data_message
{
public:
  enum { header_length = 4 };
  enum { max_body_length = SGX_INPUT_MAX };

  data_message()
    : body_length_(0)
  {
  }

  const char* data() const
  {
    return data_;
  }

  char* data()
  {
    return data_;
  }

  std::size_t length() const
  {
    return header_length + body_length_;
  }

  const char* body() const
  {
    return data_ + header_length;
  }

  char* body()
  {
    return data_ + header_length;
  }

  std::size_t body_length() const
  {
    return body_length_;
  }

  void body_length(std::size_t new_length)
  {
    body_length_ = new_length;
    if (body_length_ > max_body_length)
      body_length_ = max_body_length;
  }

  bool decode_header()
  {
    char header[header_length + 1] = "";
    std::strncat(header, data_, header_length);
    body_length_ = std::atoi(header);
    if (body_length_ > max_body_length)
    {
      body_length_ = 0;
      return false;
    }
    return true;
  }

  void encode_header()
  {
    char header[header_length + 1] = "";
    std::sprintf(header, "%4d", static_cast<int>(body_length_));
    std::memcpy(data_, header, header_length);
  }

private:
  char data_[header_length + max_body_length];
  std::size_t body_length_;
};

#endif // data_message_HPP


//----------------------------------------------------------------------

typedef std::deque<data_message> data_message_queue;

//----------------------------------------------------------------------

class comm_participant
{
public:
  virtual ~comm_participant() {}
  virtual void deliver(const data_message& msg) = 0;
};

typedef std::shared_ptr<comm_participant> comm_participant_ptr;

//----------------------------------------------------------------------


//----------------------------------------------------------------------

class comm_session
  : public comm_participant,
    public std::enable_shared_from_this<comm_session>
{
public:
  comm_session(tcp::socket socket, tcp::socket out_socket, uint32_t req)
    : socket_(std::move(socket)),
      out_socket_(std::move(out_socket)),
      req_out_(req)
  {
  }

  void start()
  {
    do_read_header();
  }

  void deliver(const data_message& msg)
  {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      do_write();
    }
  }

private:
  void do_read_header()
  {
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(read_msg_.data(), data_message::header_length),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec && read_msg_.decode_header())
          {
            do_read_body();
          }
          else
          {
            std::cout << ec.message() << std::endl;
          }
        });
  }

  void do_read_body()
  {
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            //room_.deliver(read_msg_);
            //std::cout.write(read_msg_.body(), read_msg_.body_length());
            std::cout << "received " << read_msg_.body_length() << std::endl;

            size_t result_size = 0;
            #ifndef USE_TEST
            result_size =
            do_infer(read_msg_.body_length(),
                     asio::buffer_cast<const char *>(read_msg_.body()),
                     sizeof(sgx_output), sgx_output);
            #else
            result_size =
            do_infer(0, nullptr,
                     sizeof(sgx_output), sgx_output);
            #endif
            if(req_out_){
              data_message msg;
              msg.body_length(result_size);
              std::memcpy(msg.body(), sgx_output, msg.body_length());
              msg.encode_header();
              deliver(msg);
            }
            do_read_header();
          }
          else
          {
            std::cout << ec.message() << std::endl;
          }
        });
  }

  void do_write()
  {
    auto self(shared_from_this());
    asio::async_write(out_socket_,
        asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              do_write();
            }
          }
          else
          {
            std::cout << ec.message() << std::endl;
            //room_.leave(shared_from_this());
          }
        });
  }

  tcp::socket socket_;
  tcp::socket out_socket_;
  data_message read_msg_;
  data_message_queue write_msgs_;
  uint32_t req_out_;
};

//----------------------------------------------------------------------

class middle_server
{
public:
  middle_server(asio::io_context& io_context,
      const tcp::endpoint& endpoint_in,
      const tcp::resolver::results_type& endpoint_out,
      uint32_t out_req)
    : acceptor_(io_context, endpoint_in),
    out_socket(io_context),
    req_out(out_req)
  {
    if(req_out){
      do_connect(endpoint_out);
    }
    do_accept();
  }


private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](std::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<comm_session>(std::move(socket), std::move(out_socket), req_out)->start();
          }

          do_accept();
        });
  }

  void do_connect(const tcp::resolver::results_type& endpoints)
  {
    asio::async_connect(out_socket, endpoints,
        [this](std::error_code ec, tcp::endpoint)
        {
          if (!ec)
          {
            //do_read_header();
          }
        });
  }



  tcp::acceptor acceptor_;
  tcp::socket out_socket;
  uint32_t req_out;
};



typedef std::deque<data_message> data_message_queue;

class message_client
{
public:
  message_client(asio::io_context& io_context,
      const tcp::resolver::results_type& endpoints)
    : io_context_(io_context),
      socket_(io_context)
  {
    do_connect(endpoints);
  }

  void write(const data_message& msg)
  {
    asio::post(io_context_,
        [this, msg]()
        {
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          if (!write_in_progress)
          {
            do_write();
          }
        });
  }

  void close()
  {
    asio::post(io_context_, [this]() { socket_.close(); });
  }

private:
  void do_connect(const tcp::resolver::results_type& endpoints)
  {
    asio::async_connect(socket_, endpoints,
        [this](std::error_code ec, tcp::endpoint)
        {
          if (!ec)
          {
            do_read_header();
          }
        });
  }

  void do_read_header()
  {
    asio::async_read(socket_,
        asio::buffer(read_msg_.data(), data_message::header_length),
        [this](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec && read_msg_.decode_header())
          {
            do_read_body();
          }
          else
          {
            socket_.close();
          }
        });
  }

  void do_read_body()
  {
    asio::async_read(socket_,
        asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            std::cout.write(read_msg_.body(), read_msg_.body_length());
            std::cout << "\n";
            do_read_header();
          }
          else
          {
            socket_.close();
          }
        });
  }

  void do_write()
  {
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              do_write();
            }
          }
          else
          {
            socket_.close();
          }
        });
  }

private:
  asio::io_context& io_context_;
  tcp::socket socket_;
  data_message read_msg_;
  data_message_queue write_msgs_;
};

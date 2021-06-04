//
// data_message.hpp
// ~~~~~~~~~~~~~~~~
//
// Shixiong Jing Modified from asio documentation



#ifndef data_message_HPP
#define data_message_HPP


#include "common.hpp"

using asio::ip::tcp;

//uint64_t comm_gap_time = 0;

struct message_header
    {
      uint32_t size = 0;
      uint32_t message_type = 0;// 0 for sending package, 1 for successful replies, 2 for waiting, 3 for error
      uint32_t id1 = 1;
      uint32_t id2 = 1;
      uint64_t timestamp = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());
    };

struct data_message
{

  std::vector<char> data_buf;
  message_header header;
  size_t size() const
      {
        return data_buf.size();
      }

  
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
      req_out_(req),
      queued_message_num(0),
      my_timer(socket.get_executor())
  {
  }

  void start()
  {
    std::cout << "start channel" << std::endl;
    do_read_header();
  }

  void deliver(const data_message& msg)
  {
    if(msg.header.message_type==0){
      queued_message_num++;
      if(queued_message_num > 5){ overheat = 1; }
      //std::cout << "now overheat " << overheat << " qn: " << queued_message_num << std::endl;
    }
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      do_write_header();
    }
  }

private:
  //ise lambda function might be the only way
  void do_wait_write(){
    auto self(shared_from_this());
    std::cout << "waiting with overheat "<< overheat << " queued message " << queued_message_num << std::endl;
    my_timer.expires_from_now(std::chrono::seconds(1));
    my_timer.async_wait(
      [this, self](const asio::error_code& ec){
        if(!ec){
          if(!overheat){
            do_write_header();
          }
          else{
            do_wait_write();
          }
        }
        else{
          std::cout << "[Error] in server do wait write: " << ec.message() << std::endl;
        }
      }
    );
    
  }

  void do_read_header()
  {
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(&read_msg_.header, sizeof(message_header)),
        [this, self](std::error_code ec, std::size_t n/*length*/)
        {
          if (!ec)
          {
            if(read_msg_.header.size > 0){
              read_msg_.data_buf.resize(read_msg_.header.size);
              do_read_body();
            }
            else{
              do_read_header();
            }
            // else if(read_msg_.header.message_type == 1){
            //   queued_message_num--;
            //   if(queued_message_num < 3){ overheat = 0; }
            //   std::cout << "$$$ got reply now overheat " << overheat << " qn: " << queued_message_num << std::endl;
            // }
          }
          else
          {
            std::cout << "Received:" << n << std::endl;
            std::cout << "[Error] in server do read header " << ec.message() << std::endl;
          }
        });
  }

  void do_read_reply()
  {
    auto self(shared_from_this());
    asio::async_read(out_socket_,
        asio::buffer(&read_msg_.header, sizeof(message_header)),
        [this, self](std::error_code ec, std::size_t n/*length*/)
        {
          if (!ec)
          {
            // if(read_msg_.header.size > 0){
            //   read_msg_.data_buf.resize(read_msg_.header.size);
            //   do_read_body();
            // }
            if(read_msg_.header.message_type == 1){
              queued_message_num--;
              if(queued_message_num < 3){ overheat = 0; }
              std::cout << "$$$ got reply now overheat " << overheat << " qn: " << queued_message_num << std::endl;
            }
          }
          else
          {
            std::cout << "Received:" << n << std::endl;
            std::cout << "[Error] in server do read reply header " << ec.message() << std::endl;
          }
        });
  }

  void do_read_body()
  {
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(read_msg_.data_buf),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            //std::cout << "@@@ received data " << read_msg_.data_buf.size() << std::endl;

            comm_gap_time += uint64_t(std::chrono::system_clock::now().time_since_epoch().count()) - read_msg_.header.timestamp;
            size_t result_size = 0;
            
            #ifndef USE_TEST
            result_size =
            do_infer(read_msg_.size(),
                     asio::buffer_cast<const char *>(read_msg_.data_buf.data()),
                     sizeof(sgx_output), sgx_output);
            #else
            result_size =
            do_infer(0, nullptr,
                     sizeof(sgx_output), sgx_output);
            #endif

            round_counter++;
            #ifdef PRINT_ROUND_INFO
            std::cout << "round: " << round_counter <<
            "time: " << std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
            #endif
            //std::this_thread::sleep_for (std::chrono::seconds(1));

            data_message * reply = new data_message();
            reply->header.message_type = 1;
            reply->header.size = 0;
            deliver(*reply);

            if(req_out_){
              #ifdef OUTSIDE_SGX_TEST
              result_size = SGX_OUTPUT_MAX;
              std::fill_n (sgx_output, result_size, 50);
              sgx_output[0]=(uint8_t)'A';
              #endif
              data_message * datapack = new data_message();
              datapack->data_buf.assign(sgx_output, sgx_output + result_size);
              datapack->header.size = datapack->data_buf.size();
              datapack->header.message_type = 0;
            
              deliver(*datapack);
            }
            

            do_read_header();
          }
          else
          {
            std::cout << "error in server do read body" << ec.message() << std::endl;
          }
        });
  }

  void do_write_header()
  {
    auto self(shared_from_this());
    if(write_msgs_.front().header.message_type == 0){
      asio::async_write(out_socket_,
          asio::buffer(&write_msgs_.front().header,
            sizeof(message_header)),
          [this, self](std::error_code ec, std::size_t /*length*/)
          {
            if (!ec)
            {
              if (write_msgs_.front().data_buf.size() > 0)
              {
                do_write_body();
              } else{
                write_msgs_.pop_front();
                if (!write_msgs_.empty())
                {
                  if(overheat){
                    //do_wait_write();
                    do_write_header();
                  }
                  else{
                    do_write_header();
                  }
                }
              }
            }
            else
            {
              std::cout << "error in server do write header" << ec.message() << std::endl;
              out_socket_.close();
              //room_.leave(shared_from_this());
            }
          });
    }
    else{
      asio::async_write(socket_,
          asio::buffer(&write_msgs_.front().header,
            sizeof(message_header)),
          [this, self](std::error_code ec, std::size_t n/*length*/)
          {
            std::cout << "### Replied success message:" << n << std::endl;
            if (!ec)
            {
              write_msgs_.pop_front();
              if (!write_msgs_.empty())
              {
                do_write_header();
              }
            }
            else
            {
              std::cout << "error in server do write reply header" << ec.message() << std::endl;
              out_socket_.close();
              //room_.leave(shared_from_this());
            }
          });
    }
  }

  void do_write_body()
  {
    auto self(shared_from_this());
    asio::async_write(out_socket_,
        asio::buffer(write_msgs_.front().data_buf),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              //do_write_header();
              if(overheat){
                //do_wait_write();
                do_write_header();
              }
              else{
                do_write_header();
              }
            }
          }
          else
          {
            std::cout << "error in server do write body" << ec.message() << std::endl;
            out_socket_.close();
            //room_.leave(shared_from_this());
          }
        });
  }



  tcp::socket socket_;
  tcp::socket out_socket_;
  data_message read_msg_;
  data_message_queue write_msgs_;
  uint32_t req_out_;
  uint32_t queued_message_num;
  asio::steady_timer my_timer;
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
            std::cout << "accepted connection" << std::endl;
            std::make_shared<comm_session>(std::move(socket), std::move(out_socket), req_out)->start();
          }
          else
          {
            std::cout << "error in server accept" << ec.message() << std::endl;
            //room_.leave(shared_from_this());
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
          else
          {
            std::cout << "error in server do connect" << ec.message() << std::endl;
            //room_.leave(shared_from_this());
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
      socket_(io_context),
      queued_message_num(0)
  {
    do_connect(endpoints);
  }

  void write(const data_message& msg)
  {
    queued_message_num++;
    if(queued_message_num > 5){ overheat = 1; }
    asio::post(io_context_,
        [this, msg]()
        {
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          if (!write_in_progress)
          {
            std::cout << "writing header" << std::endl;
            do_write_header();
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
          else
          {
            std::cout << "error in client do connect" << ec.message() << std::endl;
          }
        });
  }

  

  void do_read_header()
  {
    asio::async_read(socket_,
        asio::buffer(&read_msg_.header, sizeof(message_header)),
        [this](std::error_code ec, std::size_t n/*length*/)
        {
          if (!ec)
          {
            std::cout << "###  Received:" << n << std::endl;
            if(--queued_message_num < 3){ overheat = 0; }
            if(read_msg_.header.size > 0){
              read_msg_.data_buf.resize(read_msg_.header.size);
              do_read_body();
            }
            else{
              //add to read message queue
              do_read_header();
            }
          }
          else
          {
            std::cout << "[Error] in client do read header " << ec.message() << std::endl;
            //socket_.close();
          }
        });
  }

  void do_read_body()
  {
    asio::async_read(socket_,
        asio::buffer(read_msg_.data_buf),
        [this](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            //room_.deliver(read_msg_);
            std::cout.write(read_msg_.data_buf.data(), read_msg_.size());
            std::cout << "\n";
            do_read_header();
          }
          else
          {
            std::cout << "error in client do read body " << ec.message() << std::endl;
            socket_.close();
          }
        });
  }

  void do_write_header()
  {
    asio::async_write(socket_,
        asio::buffer(&write_msgs_.front().header, sizeof(message_header)),
        [this](std::error_code ec, std::size_t n/*length*/)
        {
          //std::cout << "header written " << n << std::endl;
          if (!ec)
          {
            if (write_msgs_.front().data_buf.size() > 0)
            {
              do_write_body();
            } else{
              write_msgs_.pop_front();
              if (!write_msgs_.empty())
              {
                do_write_header();
              }
            }
          }
          else
          {
            std::cout << "error in client do write header" << ec.message() << std::endl;
            socket_.close();
            //room_.leave(shared_from_this());
          }
        });
  }

  void do_write_body()
  {
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data_buf),
        [this](std::error_code ec, std::size_t n/*length*/)
        {
          std::cout << "actual write " << n << std::endl;
          if (!ec)
          {
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              do_write_header();
            }
          }
          else
          {
            std::cout << "error in client do write body" << ec.message() << std::endl;
            socket_.close();
            //room_.leave(shared_from_this());
          }
        });
  }

private:
  asio::io_context& io_context_;
  tcp::socket socket_;
  data_message read_msg_;
  data_message_queue write_msgs_;
  uint32_t queued_message_num;
};

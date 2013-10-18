#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif


#include <string>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
using namespace boost::asio;
io_service service;

struct async_op : boost::enable_shared_from_this<async_op>
               , private boost::noncopyable {
    typedef boost::shared_ptr<async_op> ptr;
    static ptr new_() { return ptr(new async_op); }
private:
    typedef boost::function<void(boost::system::error_code)> completion_func;
    typedef boost::function<boost::system::error_code ()> op_func;
    async_op() : started_(false) {}
    struct operation {
        operation(io_service & service, op_func op, completion_func completion)
            : service(&service), op(op), completion(completion)
            , work(new io_service::work(service))
        {}
        operation() : service(0) {}
        io_service * service;
        op_func op;
        completion_func completion;
        typedef boost::shared_ptr<io_service::work> work_ptr;
        work_ptr work;
    };
    typedef std::vector<operation> array;
    void start() {
        { boost::recursive_mutex::scoped_lock lk(cs_);
          if ( started_)
              return;
        started_ = true; }
        boost::thread t( boost::bind(&async_op::run,this));
    }
    void run() {
        while ( true) {
            { boost::recursive_mutex::scoped_lock lk(cs_);
              if ( !started_) break; 
            }
            boost::this_thread::sleep( boost::posix_time::millisec(10));
            operation cur;
            { boost::recursive_mutex::scoped_lock lk(cs_);
              if ( !ops_.empty()) {
                  cur = ops_[0];
                  ops_.erase( ops_.begin());
              }}
            if ( cur.service) {
                boost::system::error_code err = cur.op();
                cur.service->post(boost::bind(cur.completion, err));
            }
        }
        self_.reset();
    }
public:
    void add(op_func op, completion_func completion, io_service & service) {
        // so that we're not destroyed while async-executing something
        self_ = shared_from_this();
        boost::recursive_mutex::scoped_lock lk(cs_);
        ops_.push_back( operation(service, op, completion));
        if ( !started_) start();
    }
    void stop() {
        boost::recursive_mutex::scoped_lock lk(cs_);
        started_ = false;
        ops_.clear();
    }
private:
    array ops_;
    bool started_;
    boost::recursive_mutex cs_;
    ptr self_;
};

size_t checksum = 0;
boost::system::error_code compute_file_checksum(std::string file_name) {
    HANDLE file = ::CreateFile(file_name.c_str(), GENERIC_READ, 0, 0, 
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);
    windows::random_access_handle h(service, file);
    long buff[1024];
    checksum = 0;
    size_t bytes = 0;
    size_t at = 0;
    boost::system::error_code ec;
    while ( (bytes = read_at(h, at, buffer(buff), ec)) > 0) {
        at += bytes;
        bytes /= sizeof(long);
        for ( size_t i = 0; i < bytes; ++i)
            checksum += buff[i];
    }
    return boost::system::error_code(0, boost::system::generic_category());
}
void on_checksum(std::string file_name, boost::system::error_code) {
    std::cout << "checksum for " << file_name << "=" << checksum << std::endl;
}

int main(int argc, char* argv[]) {
    std::string fn = "readme.txt";
    async_op::new_()->add( boost::bind(compute_file_checksum,fn),
                           boost::bind(on_checksum,fn,_1), service);
    service.run();
}


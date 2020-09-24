#pragma once

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stack>
#include <vector>

template <typename T, int num_bufs>
class TBuffer : public std::enable_shared_from_this<TBuffer<T, num_bufs>> {

  // dispatch the buffer borrowed for write
  class do_dispatch {
    std::weak_ptr<TBuffer<T, num_bufs>> tbuffer_;

  public:
    do_dispatch(const std::shared_ptr<TBuffer<T, num_bufs>> &sp)
        : tbuffer_(sp) {}

    void operator()(T *p) const {
      if (p == nullptr)
        return;
      if (auto sp = tbuffer_.lock()) {
        sp->dispatch(std::unique_ptr<T>(p));
        return;
      }
      std::default_delete<T>{}(p);
    }
  };

  // release the buffer borrowed for read
  class do_release {
    std::weak_ptr<TBuffer<T, num_bufs>> tbuffer_;

  public:
    do_release(const std::shared_ptr<TBuffer<T, num_bufs>> &sp)
        : tbuffer_(sp) {}

    void operator()(T *p) const {
      if (p == nullptr)
        return;
      if (auto sp = tbuffer_.lock()) {
        sp->release(std::unique_ptr<T>(p));
        return;
      }
      std::default_delete<T>{}(p);
    }
  };

  std::mutex m_;
  std::condition_variable cv_;
  std::vector<bool> reading_;
  int pending_idx_;
  bool stopped_;
  std::array<std::unique_ptr<T>, num_bufs> arr_;

  // init a tbuffer struct
  template <typename... Args> TBuffer(Args &&... args) : pending_idx_(-1) {
    reading_ = std::vector<bool>(num_bufs, false);
    for (int i = 0; i < num_bufs; ++i) {
      arr_[i] = (std::make_unique<T>(std::forward<Args>(args)...));
    }
  }

public:
  template <typename... Args>
  static std::shared_ptr<TBuffer<T, num_bufs>> make(Args &&... args) {
    return std::shared_ptr<TBuffer<T, num_bufs>>(new TBuffer(std::forward<Args>(args)...));
  }

  using write_ptr_type = std::unique_ptr<T, do_dispatch>;
  using read_ptr_type = std::unique_ptr<T, do_release>;

  // select a buffer not being read for write
  // returns nullptr if failed
  write_ptr_type select() {
    std::lock_guard<std::mutex> lock(m_);
    int i = 0;
    for (i = 0; i < num_bufs; ++i) {
      if (!reading_[i] && i != pending_idx_ && arr_[i] != nullptr) {
        break;
      }
    }
    write_ptr_type tmp{arr_[i].release(), this->shared_from_this()};
    return tmp;
  }

  // release a buffer after write
  void dispatch(std::unique_ptr<T> elem) {
    {
      std::unique_lock<std::mutex> lock(m_);
      if (pending_idx_ != -1) {
        auto pending = std::move(arr_[pending_idx_]);
        arr_[pending_idx_] = std::move(elem);

        int idx;
        for (idx = 0; idx < arr_.size(); ++idx) {
          if (arr_[idx] == nullptr) {
            arr_[idx] = std::move(pending);
            pending_idx_ = idx;
            break;
          }
        }
        if (idx == arr_.size())
          abort();
      } else {
        int idx;
        for (idx = 0; idx < arr_.size(); ++idx) {
          if (arr_[idx] == nullptr) {
            break;
          }
        }
        arr_[idx] = std::move(elem);
        pending_idx_ = idx;
      }
    }
    cv_.notify_one();
  }

  // select a buffer which will be read and processed
  read_ptr_type acquire() {
    std::unique_lock<std::mutex> lock(m_);

    if (stopped_)
      return {nullptr, this->shared_from_this()};

    cv_.wait(lock, [this] { return pending_idx_ != -1; });

    if (stopped_)
      return {nullptr, this->shared_from_this()};

    int ret = pending_idx_;
    reading_[ret] = true;
    pending_idx_ = -1;
    read_ptr_type tmp{arr_[ret].release(), this->shared_from_this()};
    return tmp;
  }

  // release a buffer after reading/processing
  void release(std::unique_ptr<T> elem) {
    std::lock_guard<std::mutex> lock(m_);
    for (int i = 0; i < arr_.size(); ++i) {
      if (arr_[i] == nullptr) {
        arr_[i] = std::move(elem);
        reading_[i] = false;
      }
    }
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(m_);
      stopped_ = true;
    }
    cv_.notify_one();
  }
};

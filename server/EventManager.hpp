#ifndef EVENTMANAGER_HPP
#define EVENTMANAGER_HPP

#include <vector>
#include <map>
#include <poll.h>
#include <cerrno>
#include <cstring>

class EventManager {
public:
    struct Event {
        int fd;
        bool readable;
        bool writable;
        bool error;
    };
    
private:
    std::vector<struct pollfd> pollfds;
    std::map<int, std::size_t> fd_to_index;
    std::vector<Event> events;
    
public:
    EventManager();
    ~EventManager();
    
    void addFd(int fd, bool monitor_read = true, bool monitor_write = false);
    void removeFd(int fd);
    void setWriteMonitoring(int fd, bool enable);
    void setReadMonitoring(int fd, bool enable);
    
    int wait(int timeout_ms = -1);
    
    const std::vector<Event>& getEvents() const { return events; }
    
    bool isMonitored(int fd) const;
    
private:
    void updatePollfd(int fd);
};

#endif
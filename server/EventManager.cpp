#include "EventManager.hpp"
#include <stdexcept>
#include <cstring>
#include <unistd.h>

EventManager::EventManager() {}

EventManager::~EventManager() {}

void EventManager::addFd(int fd, bool monitor_read, bool monitor_write) {
    if (fd_to_index.find(fd) != fd_to_index.end())
        return;
    
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = 0;
    
    if (monitor_read)
        pfd.events |= POLLIN;
    if (monitor_write)
        pfd.events |= POLLOUT;
    
    pfd.revents = 0;
    
    fd_to_index[fd] = pollfds.size();
    pollfds.push_back(pfd);
}

void EventManager::removeFd(int fd) {
    std::map<int, size_t>::iterator it = fd_to_index.find(fd);
    if (it == fd_to_index.end())
        return;
    
    size_t index = it->second;
    
    if (index < pollfds.size() - 1) {
        pollfds[index] = pollfds.back();
        fd_to_index[pollfds[index].fd] = index;
    }
    
    pollfds.pop_back();
    fd_to_index.erase(it);
}

void EventManager::setWriteMonitoring(int fd, bool enable) {
    std::map<int, size_t>::iterator it = fd_to_index.find(fd);
    if (it == fd_to_index.end())
        return;
    
    if (enable)
        pollfds[it->second].events |= POLLOUT;
    else
        pollfds[it->second].events &= ~POLLOUT;
}

void EventManager::setReadMonitoring(int fd, bool enable) {
    std::map<int, size_t>::iterator it = fd_to_index.find(fd);
    if (it == fd_to_index.end())
        return;
    
    if (enable)
        pollfds[it->second].events |= POLLIN;
    else
        pollfds[it->second].events &= ~POLLIN;
}

int EventManager::wait(int timeout_ms) {
    events.clear();
    
    if (pollfds.empty())
        return 0;
    
    int ret = poll(&pollfds[0], pollfds.size(), timeout_ms);
    
    if (ret < 0) {
        if (errno == EINTR)
            return 0;
        throw std::runtime_error("poll() failed: " + std::string(strerror(errno)));
    }
    
    if (ret == 0)
        return 0;
    
    for (size_t i = 0; i < pollfds.size(); i++) {
        if (pollfds[i].revents != 0) {
            Event e;
            e.fd = pollfds[i].fd;
            e.readable = (pollfds[i].revents & POLLIN) != 0;
            e.writable = (pollfds[i].revents & POLLOUT) != 0;
            e.error = (pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            events.push_back(e);
        }
    }
    
    return events.size();
}

bool EventManager::isMonitored(int fd) const {
    return fd_to_index.find(fd) != fd_to_index.end();
}
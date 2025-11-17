#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"

extern std::string intToString(int value);
extern std::string sizeToString(size_t value);

void Server::_parseCommand(Client* client, const std::string& command) {
    std::vector<std::string> tokens = _splitMessage(command);
    if (tokens.empty()) return;
    
    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    
    std::vector<std::string> params(tokens.begin() + 1, tokens.end());
    
    if (cmd == "CAP") {
        if (!params.empty() && params[0] == "LS")
            _sendToClient(client->getFd(), "CAP * LS :");
        return;
    }
    
    if (cmd == "PASS")
        _handlePass(client, params);
    else if (cmd == "NICK")
        _handleNick(client, params);
    else if (cmd == "USER")
        _handleUser(client, params);
    else if (cmd == "JOIN")
        _handleJoin(client, params);
    else if (cmd == "PART")
        _handlePart(client, params);
    else if (cmd == "PRIVMSG" || cmd == "NOTICE")
        _handlePrivmsg(client, params);
    else if (cmd == "QUIT")
        _handleQuit(client, params);
    else if (cmd == "PING")
        _handlePing(client, params);
    else if (cmd == "PONG")
        return;
    else if (cmd == "KICK")
        _handleKick(client, params);
    else if (cmd == "INVITE")
        _handleInvite(client, params);
    else if (cmd == "TOPIC")
        _handleTopic(client, params);
    else if (cmd == "MODE")
        _handleMode(client, params);
    else if (cmd == "WHO")
        _handleWho(client, params);
    else if (cmd == "WHOIS")
        _handleWhois(client, params);
    else if (cmd == "LIST")
        _handleList(client, params);
    else if (cmd == "NAMES")
        _handleNames(client, params);
    else if (cmd == "MOTD")
        _handleMotd(client, params);
    else if (client->isRegistered())
        _sendNumericReply(client, ERR_UNKNOWNCOMMAND, cmd + " :Unknown command");
}

void Server::_handlePass(Client* client, const std::vector<std::string>& params) {
    if (client->isRegistered()) {
        _sendNumericReply(client, ERR_ALREADYREGISTRED, ":You may not reregister");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "PASS :Not enough parameters");
        return;
    }
    
    if (!isValidPassword(params[0])) {
        _sendNumericReply(client, ERR_PASSWDMISMATCH, ":Password incorrect");
        _disconnectClient(client->getFd(), "Bad password");
        return;
    }
    
    client->setPasswordProvided(true);
    client->tryRegister();
    if (client->isRegistered())
        _sendWelcomeSequence(client);
}

void Server::_handleNick(Client* client, const std::vector<std::string>& params) {
    if (!client->hasPasswordProvided() && !_password.empty()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":Password required");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NONICKNAMEGIVEN, ":No nickname given");
        return;
    }
    
    std::string newNick = params[0];
    
    if (!_isValidNickname(newNick)) {
        _sendNumericReply(client, ERR_ERRONEUSNICKNAME, newNick + " :Erroneous nickname");
        return;
    }
    
    Client* existingClient = getClientByNick(newNick);
    if (existingClient && existingClient != client) {
        _sendNumericReply(client, ERR_NICKNAMEINUSE, newNick + " :Nickname is already in use");
        return;
    }
    
    std::string oldNick = client->getNickname();
    client->setNickname(newNick);
    
    if (client->isRegistered()) {
        std::string nickMsg = ":" + oldNick + "!" + client->getUsername() + "@" + client->getHostname() + " NICK :" + newNick;
        
        _sendToClient(client->getFd(), nickMsg);
        
        std::set<Client*> notifiedClients;
        notifiedClients.insert(client);
        
        std::set<Channel*> channels = client->getChannels();
        for (std::set<Channel*>::iterator it = channels.begin(); it != channels.end(); ++it) {
            const std::set<Client*>& channelClients = (*it)->getClients();
            for (std::set<Client*>::const_iterator cIt = channelClients.begin(); cIt != channelClients.end(); ++cIt) {
                if (notifiedClients.find(*cIt) == notifiedClients.end()) {
                    _sendToClient((*cIt)->getFd(), nickMsg);
                    notifiedClients.insert(*cIt);
                }
            }
        }
        
        _logMessage("INFO", "Nick change: " + oldNick + " -> " + newNick);
    } else {
        client->tryRegister();
        if (client->isRegistered())
            _sendWelcomeSequence(client);
    }
}

void Server::_handleUser(Client* client, const std::vector<std::string>& params) {
    if (!client->hasPasswordProvided() && !_password.empty()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":Password required");
        return;
    }
    
    if (client->isRegistered()) {
        _sendNumericReply(client, ERR_ALREADYREGISTRED, ":You may not reregister");
        return;
    }
    
    if (params.size() < 4) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "USER :Not enough parameters");
        return;
    }
    
    client->setUsername(params[0]);
    client->setRealname(params[3]);
    
    client->tryRegister();
    if (client->isRegistered())
        _sendWelcomeSequence(client);
}

void Server::_handleJoin(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "JOIN :Not enough parameters");
        return;
    }
    
    if (params[0] == "0") {
        std::set<Channel*> channels = client->getChannels();
        for (std::set<Channel*>::iterator it = channels.begin(); it != channels.end(); ++it) {
            std::string partMsg = ":" + client->getPrefix() + " PART " + (*it)->getName() + " :Leaving all channels";
            _sendToChannel(*it, partMsg);
            client->leaveChannel(*it);
        }
        return;
    }
    
    std::istringstream channelStream(params[0]);
    std::istringstream keyStream(params.size() > 1 ? params[1] : "");
    std::string channelName, key;
    
    while (std::getline(channelStream, channelName, ',')) {
        if (channelName.empty()) continue;
        
        std::getline(keyStream, key, ',');
        
        if (!_isValidChannelName(channelName)) {
            _sendNumericReply(client, ERR_NOSUCHCHANNEL, channelName + " :No such channel");
            continue;
        }
        
        if (!client->canJoinMoreChannels()) {
            _sendNumericReply(client, ERR_TOOMANYCHANNELS, channelName + " :You have joined too many channels");
            break;
        }
        
        Channel* channel = _getOrCreateChannel(channelName);
        
        if (channel->hasClient(client)) continue;
        
        if (!channel->canJoin(client, key)) {
            if (channel->getUserLimit() > 0 && channel->getClientCount() >= static_cast<size_t>(channel->getUserLimit()))
                _sendNumericReply(client, ERR_CHANNELISFULL, channelName + " :Cannot join channel (+l)");
            else if (channel->isInviteOnly() && !channel->isInvited(client))
                _sendNumericReply(client, ERR_INVITEONLYCHAN, channelName + " :Cannot join channel (+i)");
            else if (channel->hasKey() && key != channel->getKey())
                _sendNumericReply(client, ERR_BADCHANNELKEY, channelName + " :Cannot join channel (+k)");
            else if (channel->isBanned(client))
                _sendNumericReply(client, ERR_BANNEDFROMCHAN, channelName + " :Cannot join channel (+b)");
            continue;
        }
        
        client->joinChannel(channel);
        
        std::string joinMsg = ":" + client->getPrefix() + " JOIN :" + channelName;
        _sendToChannel(channel, joinMsg);
        
        if (!channel->getTopic().empty())
            _sendNumericReply(client, RPL_TOPIC, channelName + " :" + channel->getTopic());
        
        _sendNumericReply(client, RPL_NAMREPLY, "= " + channelName + " :" + channel->getNamesReply());
        _sendNumericReply(client, RPL_ENDOFNAMES, channelName + " :End of /NAMES list");
    }
}

void Server::_handlePart(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "PART :Not enough parameters");
        return;
    }
    
    std::string reason = params.size() > 1 ? params[1] : client->getNickname();
    
    std::istringstream channelStream(params[0]);
    std::string channelName;
    
    while (std::getline(channelStream, channelName, ',')) {
        if (channelName.empty()) continue;
        
        Channel* channel = getChannel(channelName);
        if (!channel) {
            _sendNumericReply(client, ERR_NOSUCHCHANNEL, channelName + " :No such channel");
            continue;
        }
        
        if (!channel->hasClient(client)) {
            _sendNumericReply(client, ERR_NOTONCHANNEL, channelName + " :You're not on that channel");
            continue;
        }
        
        std::string partMsg = ":" + client->getPrefix() + " PART " + channelName + " :" + reason;
        _sendToChannel(channel, partMsg);
        
        client->leaveChannel(channel);
    }
}

void Server::_handlePrivmsg(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NORECIPIENT, ":No recipient given (PRIVMSG)");
        return;
    }
    
    if (params.size() < 2 || params[1].empty()) {
        _sendNumericReply(client, ERR_NOTEXTTOSEND, ":No text to send");
        return;
    }
    
    std::istringstream targetStream(params[0]);
    std::string target;
    
    while (std::getline(targetStream, target, ',')) {
        if (target.empty()) continue;
        
        if (target[0] == '#' || target[0] == '&') {
            Channel* channel = getChannel(target);
            if (!channel) {
                _sendNumericReply(client, ERR_NOSUCHCHANNEL, target + " :No such channel");
                continue;
            }
            
            if (!channel->canSpeak(client)) {
                _sendNumericReply(client, ERR_CANNOTSENDTOCHAN, target + " :Cannot send to channel");
                continue;
            }
            
            std::string msg = ":" + client->getPrefix() + " PRIVMSG " + target + " :" + params[1];
            _sendToChannel(channel, msg, client);
        } else {
            Client* targetClient = getClientByNick(target);
            if (!targetClient) {
                _sendNumericReply(client, ERR_NOSUCHNICK, target + " :No such nick/channel");
                continue;
            }
            
            std::string msg = ":" + client->getPrefix() + " PRIVMSG " + target + " :" + params[1];
            _sendToClient(targetClient->getFd(), msg);
        }
    }
}

void Server::_handleQuit(Client* client, const std::vector<std::string>& params) {
    std::string reason = params.empty() ? "Client Quit" : params[0];
    _disconnectClient(client->getFd(), reason);
}

void Server::_handlePing(Client* client, const std::vector<std::string>& params) {
    if (params.empty()) {
        _sendNumericReply(client, ERR_NOORIGIN, ":No origin specified");
        return;
    }
    
    _sendToClient(client->getFd(), ":" + _serverName + " PONG " + _serverName + " :" + params[0]);
}

void Server::_handleKick(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.size() < 2) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "KICK :Not enough parameters");
        return;
    }
    
    Channel* channel = getChannel(params[0]);
    if (!channel) {
        _sendNumericReply(client, ERR_NOSUCHCHANNEL, params[0] + " :No such channel");
        return;
    }
    
    if (!channel->isOperator(client)) {
        _sendNumericReply(client, ERR_CHANOPRIVSNEEDED, params[0] + " :You're not channel operator");
        return;
    }
    
    std::string reason = params.size() > 2 ? params[2] : client->getNickname();
    
    std::istringstream nickStream(params[1]);
    std::string targetNick;
    
    while (std::getline(nickStream, targetNick, ',')) {
        Client* target = getClientByNick(targetNick);
        if (!target) {
            _sendNumericReply(client, ERR_NOSUCHNICK, targetNick + " :No such nick");
            continue;
        }
        
        if (!channel->hasClient(target)) {
            _sendNumericReply(client, ERR_USERNOTINCHANNEL, targetNick + " " + params[0] + " :They aren't on that channel");
            continue;
        }
        
        std::string kickMsg = ":" + client->getPrefix() + " KICK " + params[0] + " " + targetNick + " :" + reason;
        _sendToChannel(channel, kickMsg);
        
        target->leaveChannel(channel);
    }
}

void Server::_handleInvite(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.size() < 2) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "INVITE :Not enough parameters");
        return;
    }
    
    Client* target = getClientByNick(params[0]);
    if (!target) {
        _sendNumericReply(client, ERR_NOSUCHNICK, params[0] + " :No such nick");
        return;
    }
    
    Channel* channel = getChannel(params[1]);
    if (!channel) {
        _sendNumericReply(client, ERR_NOSUCHCHANNEL, params[1] + " :No such channel");
        return;
    }
    
    if (!channel->hasClient(client)) {
        _sendNumericReply(client, ERR_NOTONCHANNEL, params[1] + " :You're not on that channel");
        return;
    }
    
    if (channel->isInviteOnly() && !channel->isOperator(client)) {
        _sendNumericReply(client, ERR_CHANOPRIVSNEEDED, params[1] + " :You're not channel operator");
        return;
    }
    
    if (channel->hasClient(target)) {
        _sendNumericReply(client, ERR_USERONCHANNEL, params[0] + " " + params[1] + " :is already on channel");
        return;
    }
    
    channel->addInvited(target);
    _sendNumericReply(client, RPL_INVITING, params[0] + " " + params[1]);
    _sendToClient(target->getFd(), ":" + client->getPrefix() + " INVITE " + params[0] + " :" + params[1]);
}

void Server::_handleTopic(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "TOPIC :Not enough parameters");
        return;
    }
    
    Channel* channel = getChannel(params[0]);
    if (!channel) {
        _sendNumericReply(client, ERR_NOSUCHCHANNEL, params[0] + " :No such channel");
        return;
    }
    
    if (!channel->hasClient(client)) {
        _sendNumericReply(client, ERR_NOTONCHANNEL, params[0] + " :You're not on that channel");
        return;
    }
    
    if (params.size() == 1) {
        if (channel->getTopic().empty())
            _sendNumericReply(client, RPL_NOTOPIC, params[0] + " :No topic is set");
        else
            _sendNumericReply(client, RPL_TOPIC, params[0] + " :" + channel->getTopic());
    } else {
        if (channel->isTopicRestricted() && !channel->isOperator(client)) {
            _sendNumericReply(client, ERR_CHANOPRIVSNEEDED, params[0] + " :You're not channel operator");
            return;
        }
        
        channel->setTopic(params[1], client);
        std::string topicMsg = ":" + client->getPrefix() + " TOPIC " + params[0] + " :" + params[1];
        _sendToChannel(channel, topicMsg);
    }
}

void Server::_handleMode(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "MODE :Not enough parameters");
        return;
    }
    
    if (params[0][0] != '#' && params[0][0] != '&') {
        if (params[0] != client->getNickname())
            _sendNumericReply(client, ERR_USERSDONTMATCH, ":Cannot change mode for other users");
        return;
    }
    
    Channel* channel = getChannel(params[0]);
    if (!channel) {
        _sendNumericReply(client, ERR_NOSUCHCHANNEL, params[0] + " :No such channel");
        return;
    }
    
    if (params.size() == 1) {
        _sendNumericReply(client, RPL_CHANNELMODEIS, params[0] + " " + channel->getModeString());
        return;
    }
    
    if (!channel->isOperator(client)) {
        _sendNumericReply(client, ERR_CHANOPRIVSNEEDED, params[0] + " :You're not channel operator");
        return;
    }
    
    std::string modes = params[1];
    size_t paramIdx = 2;
    bool adding = true;
    std::string appliedModes;
    std::string modeParams;
    
    for (size_t i = 0; i < modes.length(); i++) {
        char mode = modes[i];
        
        if (mode == '+') {
            adding = true;
            appliedModes += "+";
        } else if (mode == '-') {
            adding = false;
            appliedModes += "-";
        } else if (mode == 'i') {
            channel->setInviteOnly(adding);
            appliedModes += "i";
        } else if (mode == 't') {
            channel->setTopicRestricted(adding);
            appliedModes += "t";
        } else if (mode == 'k') {
            if (adding && paramIdx < params.size()) {
                channel->setKey(params[paramIdx]);
                modeParams += " " + params[paramIdx];
                paramIdx++;
                appliedModes += "k";
            } else if (!adding) {
                channel->removeKey();
                appliedModes += "k";
            }
        } else if (mode == 'l') {
            if (adding && paramIdx < params.size()) {
                int limit = atoi(params[paramIdx].c_str());
                channel->setUserLimit(limit);
                modeParams += " " + params[paramIdx];
                paramIdx++;
                appliedModes += "l";
            } else if (!adding) {
                channel->removeUserLimit();
                appliedModes += "l";
            }
        } else if (mode == 'o') {
            if (paramIdx < params.size()) {
                Client* target = getClientByNick(params[paramIdx]);
                if (target && channel->hasClient(target)) {
                    if (adding)
                        channel->addOperator(target);
                    else
                        channel->removeOperator(target);
                    modeParams += " " + params[paramIdx];
                    appliedModes += "o";
                }
                paramIdx++;
            }
        } else
            _sendNumericReply(client, ERR_UNKNOWNMODE, std::string(1, mode) + " :is unknown mode char to me");
    }
    
    if (appliedModes.length() > 1) {
        std::string modeMsg = ":" + client->getPrefix() + " MODE " + params[0] + " " + appliedModes + modeParams;
        _sendToChannel(channel, modeMsg);
    }
}

void Server::_handleWho(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    std::string mask = params.empty() ? "" : params[0];
    
    if (!mask.empty() && (mask[0] == '#' || mask[0] == '&')) {
        Channel* channel = getChannel(mask);
        if (channel && channel->hasClient(client)) {
            const std::set<Client*>& clients = channel->getClients();
            for (std::set<Client*>::const_iterator it = clients.begin(); it != clients.end(); ++it)
                _sendWhoReply(client, channel, *it);
        }
    }
    
    _sendNumericReply(client, RPL_ENDOFWHO, mask + " :End of /WHO list");
}

void Server::_handleWhois(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        _sendNumericReply(client, ERR_NEEDMOREPARAMS, "WHOIS :Not enough parameters");
        return;
    }
    
    Client* target = getClientByNick(params[0]);
    if (!target) {
        _sendNumericReply(client, ERR_NOSUCHNICK, params[0] + " :No such nick");
        return;
    }
    
    _sendWhoisReply(client, target);
    _sendNumericReply(client, RPL_ENDOFWHOIS, params[0] + " :End of /WHOIS list");
}

void Server::_handleList(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    (void)params;
    
    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
        if (!it->second->isSecret() || it->second->hasClient(client))
            _sendListReply(client, it->second);
    }
    
    _sendNumericReply(client, RPL_LISTEND, ":End of /LIST");
}

void Server::_handleNames(Client* client, const std::vector<std::string>& params) {
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    if (params.empty()) {
        for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
            if (!it->second->isSecret() || it->second->hasClient(client))
                _sendNumericReply(client, RPL_NAMREPLY, "= " + it->first + " :" + it->second->getNamesReply());
        }
    } else {
        std::istringstream channelStream(params[0]);
        std::string channelName;
        
        while (std::getline(channelStream, channelName, ',')) {
            Channel* channel = getChannel(channelName);
            if (channel && (!channel->isSecret() || channel->hasClient(client)))
                _sendNumericReply(client, RPL_NAMREPLY, "= " + channelName + " :" + channel->getNamesReply());
        }
    }
    
    _sendNumericReply(client, RPL_ENDOFNAMES, "* :End of /NAMES list");
}

void Server::_handleMotd(Client* client, const std::vector<std::string>& params) {
    (void)params;
    
    if (!client->isRegistered()) {
        _sendNumericReply(client, ERR_NOTREGISTERED, ":You have not registered");
        return;
    }
    
    _sendMotd(client);
}

void Server::_sendWhoReply(Client* client, Channel* channel, Client* target) {
    std::string flags = "H";
    if (channel && channel->isOperator(target))
        flags += "@";
    
    std::ostringstream oss;
    oss << (channel ? channel->getName() : "*") << " " << target->getUsername() << " "
        << target->getHostname() << " " << _serverName << " "
        << target->getNickname() << " " << flags << " :0 " << target->getRealname();
    
    _sendNumericReply(client, RPL_WHOREPLY, oss.str());
}

void Server::_sendWhoisReply(Client* client, Client* target) {
    _sendNumericReply(client, RPL_WHOISUSER, target->getNickname() + " " +
                     target->getUsername() + " " + target->getHostname() + " * :" + target->getRealname());
    
    _sendNumericReply(client, RPL_WHOISSERVER, target->getNickname() + " " +
                     _serverName + " :" + _serverName);
    
    std::string channels;
    const std::set<Channel*>& targetChannels = target->getChannels();
    for (std::set<Channel*>::const_iterator it = targetChannels.begin(); it != targetChannels.end(); ++it) {
        if (!(*it)->isSecret() || (*it)->hasClient(client)) {
            if (!channels.empty()) channels += " ";
            if ((*it)->isOperator(target)) channels += "@";
            channels += (*it)->getName();
        }
    }
    
    if (!channels.empty())
        _sendNumericReply(client, RPL_WHOISCHANNELS, target->getNickname() + " :" + channels);
    
    _sendNumericReply(client, RPL_WHOISIDLE, target->getNickname() + " " +
                     intToString(target->getIdleTime()) + " " +
                     intToString(static_cast<int>(target->getConnectTime())) + " :seconds idle, signon time");
}

void Server::_sendListReply(Client* client, Channel* channel) {
    std::ostringstream oss;
    oss << channel->getName() << " " << channel->getClientCount() << " :"
        << (channel->getTopic().empty() ? "" : channel->getTopic());
    
    _sendNumericReply(client, RPL_LIST, oss.str());
}
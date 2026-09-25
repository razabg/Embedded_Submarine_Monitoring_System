/*
 * message.h - a message sent between two combat submarines (menu
 * option 8/9). Keeps its content and a reference to the sending
 * submarine, per the Submarine base class spec (section 4).
 *
 * sender_ is a non-owning raw pointer: the fleet manager owns every
 * Submarine (via unique_ptr); Message only observes one and must
 * never participate in its lifetime, so a smart pointer here would be
 * the wrong tool -- see the ownership discussion in chat.
 */
#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>

class Submarine;

class Message
{
public:
    Message(std::string content, const Submarine &sender);

    const std::string &content() const { return content_; }
    const Submarine &sender() const { return *sender_; }

private:
    std::string content_;
    const Submarine *sender_;
};

#endif /* MESSAGE_H */

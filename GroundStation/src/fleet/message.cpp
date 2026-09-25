#include "message.h"

Message::Message(std::string content, const Submarine &sender)
    : content_(std::move(content)), sender_(&sender)
{
}

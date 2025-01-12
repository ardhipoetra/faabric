#include <faabric/util/queue.h>

namespace faabric::util {
TokenPool::TokenPool(int nTokens)
  : _size(nTokens)
{
    // Initialise all tokens as available
    for (int i = 0; i < nTokens; i++) {
        this->queue.enqueue(i);
    }
};

int TokenPool::size()
{
    return _size;
}

int TokenPool::taken()
{
    return _size - ((int)queue.size());
}

int TokenPool::free()
{
    return (int)queue.size();
}

/**
 * Blocking call to get an available token
 */
int TokenPool::getToken()
{
    if (_size == 0) {
        return -1;
    }
    
    if (std::experimental::optional<int> t = queue.dequeue())
        return *t;
    else
        return -1;
}

void TokenPool::releaseToken(int token)
{
    queue.enqueue(token);
}

void TokenPool::reset()
{
    queue.reset();
}
}

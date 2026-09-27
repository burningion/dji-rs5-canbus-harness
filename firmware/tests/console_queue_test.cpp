#include "../rs5_can_monitor/ConsoleQueue.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

template <size_t N>
static std::string drain(ConsoleQueue<N> &queue, size_t usbChunk) {
  std::string output;
  while (queue.size()) {
    const size_t count = std::min(queue.contiguousSize(), usbChunk);
    output.append(reinterpret_cast<const char *>(queue.data()), count);
    queue.consume(count);
  }
  return output;
}

int main() {
  ConsoleQueue<256> queue;
  const std::string longLine = std::string(190, 'A') + "\n";
  assert(queue.append(longLine.data(), longLine.size()));
  assert(drain(queue, 64) == longLine);  // Actual TinyUSB-sized chunks.

  // Head/tail now sit near the end: the next line wraps around the buffer.
  const std::string wrapped = "start:" + std::string(170, 'B') + ":end\n";
  assert(queue.append(wrapped.data(), wrapped.size()));
  assert(drain(queue, 7) == wrapped);  // Short writes preserve byte order.

  const std::string full(256, 'C');
  assert(queue.append(full.data(), full.size()));
  assert(!queue.append("rejected\n", 9));  // Overflow rejects the whole line.
  queue.consume(0);  // A stalled/disconnected USB sink makes no progress.
  assert(queue.size() == full.size());
  assert(drain(queue, 64) == full);
  assert(queue.size() == 0);
  assert(queue.contiguousSize() == 0);

  assert(!queue.append(full.data(), 257));
  assert(queue.append("one\n", 4));
  assert(queue.append("two\n", 4));
  assert(drain(queue, 1) == "one\ntwo\n");
  std::cout << "Console queue: 64-byte USB chunks, wraparound, partial writes, and overflow passed.\n";
}

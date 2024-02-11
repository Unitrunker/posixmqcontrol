#!/bin/sh
# test for 'insane' queue names.

subject='./build/posixmqcontrol'

# does sanity check enforce leading slash?
${subject} info -q missing.leading.slash
code=$?
if [ $code != 64 ]; then
  echo $code
  exit 1
fi

# does sanity check enforce one and only one slash?
${subject} info -q /to/many/slashes
code=$?
if [ $code != 64 ]; then
  echo $code
  exit 1
fi

# does sanity check enforce length limit?
${subject} info -q /this.queue.name.is.way.too.long.at.more.than.two.hundred.and.fifty.five.characters.long.because.nobody.needs.to.type.out.something.this.ridiculously.long.than.just.goes.on.and.on.and.on.and.on.and.on.and.on.and.on.and.on.and.on.and.on.and.on.and.on.and.on
code=$?
if [ $code != 64 ]; then
  echo $code
  exit 1
fi

echo "Pass!"
exit 0

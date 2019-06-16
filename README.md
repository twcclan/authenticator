# authenticator
QMM plugin for W:ET to handle player authentication

# status
I actually planned to tweak the code slightly before releasing it, but then never got around to doing it. That is why this is a very rough initial version that will be reworked over time to have better source code quality and more solid features.

Most of the code was written ~2 years ago, and as such will probably be very inconsistent style-wise. This will be changed to be more uniform.

# license
I am not really into the legal aspects of software licenses, so I settled with GPL for now, but will probably change it later on.

# compiling
```
dpkg --add-architecture i386
apt install build-essential make gcc-multilib g++-multilib libc6-dev-i386 libcurl3:i386 libidn11-dev:i386 libssl-dev:i386
cd /usr/lib/i386-linux-gnu/
ln -s libcurl.so.4 libcurl.so
```

= FreeRADIUS Dependencies

After 15 years of not requiring any external libraries, Version 4 has
some mandatory external dependencies.

## Libraries

### libtalloc

Talloc is a memory allocation library available at https://talloc.samba.org/talloc/doc/html/index.html

**OSX**

`# brew install talloc`

**Debian / Ubuntu**

`# apt-get install libtalloc-dev`

**RedHat**

```
# subscription-manager repos --enable rhel-7-server-optional-rpms
# yum install libtalloc-dev
```

### kqueue

Kqueue is an event / timer API originally written for BSD systems.  It
is *much* simpler to use than third-party event libraries.  On Linux,
a `libkqueue` package is available.

**OSX**

_nothing to do.  kqueue is available._

**Debian / Ubuntu**

`# apt-get install libkqueue-dev`

**RedHat**

```
# subscription-manager repos --enable rhel-7-server-optional-rpms
# yum install libkqueue-dev
```

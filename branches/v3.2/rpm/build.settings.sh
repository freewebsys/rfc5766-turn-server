#!/bin/bash

# Common settings script.

TURNVERSION=3.2.2.91
BUILDDIR=~/rpmbuild
ARCH=`uname -p`
TURNSERVER_SVN_URL=http://rfc5766-turn-server.googlecode.com/svn
TURNSERVER_SVN_URL_VER=branches/v3.2

WGETOPTIONS="--no-check-certificate"
RPMOPTIONS="-ivh --force"

# DIRS

mkdir -p ${BUILDDIR}
mkdir -p ${BUILDDIR}/SOURCES
mkdir -p ${BUILDDIR}/SPECS
mkdir -p ${BUILDDIR}/RPMS
mkdir -p ${BUILDDIR}/tmp

# Common packs

PACKS="make gcc redhat-rpm-config rpm-build doxygen openssl-devel svn"
sudo yum -y install ${PACKS}
ER=$?
if ! [ ${ER} -eq 0 ] ; then
    echo "Cannot install packages ${PACKS}"
    exit -1
fi


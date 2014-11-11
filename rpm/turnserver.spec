Name:		turnserver
Version:	3.2.5.2
Release:	0%{dist}
Summary:	RFC5766 TURN Server

Group:		System Environment/Libraries
License:	BSD
URL:		https://code.google.com/p/rfc5766-turn-server/ 
Source0:	http://turnserver.open-sys.org/downloads/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:	gcc, make, redhat-rpm-config
BuildRequires:	openssl-devel, libevent-devel >= 2.0.0, postgresql-devel
BuildRequires:	hiredis-devel
Requires:	openssl, libevent >= 2.0.0, mysql-libs, postgresql-libs
Requires:	hiredis, perl-DBI, perl-libwww-perl
Requires:	telnet
%if 0%{?el6}
BuildRequires:	epel-release, mysql-devel
Requires:	epel-release, mysql-libs
%else
BuildRequires:	mariadb-devel
Requires: 	mariadb-libs
%endif


%description
The TURN Server is a VoIP media traffic NAT traversal server and gateway. It
can be used as a general-purpose network traffic TURN server/gateway, too.

This implementation also includes some extra features. Supported RFCs:

TURN specs:
- RFC 5766 - base TURN specs
- RFC 6062 - TCP relaying TURN extension
- RFC 6156 - IPv6 extension for TURN
- Experimental DTLS support as client protocol.

STUN specs:
- RFC 3489 - "classic" STUN
- RFC 5389 - base "new" STUN specs
- RFC 5769 - test vectors for STUN protocol testing
- RFC 5780 - NAT behavior discovery support

The implementation fully supports the following client-to-TURN-server protocols:
- UDP (per RFC 5766)
- TCP (per RFC 5766 and RFC 6062)
- TLS (per RFC 5766 and RFC 6062); SSL3/TLS1.0/TLS1.1/TLS1.2; SSL2 wrapping
  supported
- DTLS (experimental non-standard feature)

Supported relay protocols:
- UDP (per RFC 5766)
- TCP (per RFC 6062)

Supported user databases (for user repository, with passwords or keys, if
authentication is required):
- Flat files
- MySQL
- PostgreSQL
- Redis

Redis can also be used for status and statistics storage and notification.

Supported TURN authentication mechanisms:
- short-term
- long-term
- TURN REST API (a modification of the long-term mechanism, for time-limited
  secret-based authentication, for WebRTC applications)

The load balancing can be implemented with the following tools (either one or a
combination of them):
- network load-balancer server
- DNS-based load balancing
- built-in ALTERNATE-SERVER mechanism.


%package 	utils
Summary:	TURN client utils
Group:		System Environment/Libraries
Requires:	turnserver-client-libs = %{version}-%{release}

%description 	utils
This package contains the TURN client utils.

%package 	client-libs
Summary:	TURN client library
Group:		System Environment/Libraries
Requires:	openssl, libevent >= 2.0.0

%description	client-libs
This package contains the TURN client library.

%package 	client-devel
Summary:	TURN client development headers.
Group:		Development/Libraries
Requires:	turnserver-client-libs = %{version}-%{release}

%description 	client-devel
This package contains the TURN client development headers.

%prep
%setup -q -n %{name}-%{version}

%build
PREFIX=%{_prefix} CONFDIR=%{_sysconfdir}/%{name} EXAMPLESDIR=%{_datadir}/%{name} \
	MANPREFIX=%{_datadir} LIBDIR=%{_libdir} MORECMD=cat ./configure
make

%install
rm -rf $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT make install
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig
install -m644 rpm/turnserver.sysconfig \
		$RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig/turnserver
mv $RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnuserdb.conf.default \
	$RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnuserdb.conf
%if 0%{?el6}
cat $RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnserver.conf.default | \
    sed s/#syslog/syslog/g | \
    sed s/#no-stdout-log/no-stdout-log/g > \
    $RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnserver.conf
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/rc.d/init.d
install -m755 rpm/turnserver.init.el \
		$RPM_BUILD_ROOT/%{_sysconfdir}/rc.d/init.d/turnserver
%else
cat $RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnserver.conf.default | \
    sed s/#syslog/syslog/g | \
    sed s/#no-stdout-log/no-stdout-log/g | \
    sed s/#pidfile/pidfile/g > \
    $RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnserver.conf
mkdir -p $RPM_BUILD_ROOT/%{_unitdir}
install -m755 rpm/turnserver.service.fc \
		$RPM_BUILD_ROOT/%{_unitdir}/turnserver.service
%endif
rm -rf $RPM_BUILD_ROOT/%{_sysconfdir}/%{name}/turnserver.conf.default

%clean
rm -rf "$RPM_BUILD_ROOT"

%pre
%{_sbindir}/groupadd -r turnserver 2> /dev/null || :
%{_sbindir}/useradd -r -g turnserver -s /bin/false -c "TURN Server daemon" -d \
		%{_datadir}/%{name} turnserver 2> /dev/null || :

%post
%if 0%{?el6}
/sbin/chkconfig --add turnserver
%else
/bin/systemctl --system daemon-reload
%endif

%preun
if [ $1 = 0 ]; then
%if 0%{?el6}
	/sbin/service turnserver stop > /dev/null 2>&1
	/sbin/chkconfig --del turnserver
%else
	/bin/systemctl stop turnserver.service
	/bin/systemctl disable turnserver.service 2> /dev/null
%endif
fi

%postun
%if 0%{?fedora}
/bin/systemctl --system daemon-reload
%endif

%files
%defattr(-,root,root)
%{_bindir}/turnserver
%{_bindir}/turnadmin
%{_mandir}/man1/rfc5766-turn-server.1.gz
%{_mandir}/man1/turnserver.1.gz
%{_mandir}/man1/turnadmin.1.gz
%dir %attr(-,turnserver,turnserver) %{_sysconfdir}/%{name}
%config(noreplace) %attr(0644,turnserver,turnserver) %{_sysconfdir}/%{name}/turnserver.conf
%config(noreplace) %attr(0644,turnserver,turnserver) %{_sysconfdir}/%{name}/turnuserdb.conf
%config(noreplace) %{_sysconfdir}/sysconfig/turnserver
%if 0%{?el6}
%config %{_sysconfdir}/rc.d/init.d/turnserver
%else
%config %{_unitdir}/turnserver.service
%endif
%dir %{_docdir}/%{name}
%{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/INSTALL
%{_docdir}/%{name}/postinstall.txt
%{_docdir}/%{name}/README.turnadmin
%{_docdir}/%{name}/README.turnserver
%{_docdir}/%{name}/schema.sql
%{_docdir}/%{name}/schema.stats.redis
%{_docdir}/%{name}/schema.userdb.redis
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/schema.sql
%{_datadir}/%{name}/schema.stats.redis
%{_datadir}/%{name}/schema.userdb.redis
%{_datadir}/%{name}/testredisdbsetup.sh
%dir %{_datadir}/%{name}/etc
%{_datadir}/%{name}/etc/turn_server_cert.pem
%{_datadir}/%{name}/etc/turn_server_pkey.pem
%{_datadir}/%{name}/etc/turnserver.conf
%{_datadir}/%{name}/etc/turnuserdb.conf
%dir %{_datadir}/%{name}/scripts
%{_datadir}/%{name}/scripts/peer.sh
%{_datadir}/%{name}/scripts/readme.txt
%dir %{_datadir}/%{name}/scripts/basic
%{_datadir}/%{name}/scripts/basic/dos_attack.sh
%{_datadir}/%{name}/scripts/basic/relay.sh
%{_datadir}/%{name}/scripts/basic/tcp_client.sh
%{_datadir}/%{name}/scripts/basic/tcp_client_c2c_tcp_relay.sh
%{_datadir}/%{name}/scripts/basic/udp_c2c_client.sh
%{_datadir}/%{name}/scripts/basic/udp_client.sh
%dir %{_datadir}/%{name}/scripts/loadbalance
%{_datadir}/%{name}/scripts/loadbalance/master_relay.sh
%{_datadir}/%{name}/scripts/loadbalance/slave_relay_1.sh
%{_datadir}/%{name}/scripts/loadbalance/slave_relay_2.sh
%{_datadir}/%{name}/scripts/loadbalance/tcp_c2c_tcp_relay.sh
%{_datadir}/%{name}/scripts/loadbalance/udp_c2c.sh
%dir %{_datadir}/%{name}/scripts/longtermsecure
%{_datadir}/%{name}/scripts/longtermsecure/secure_dos_attack.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_dtls_client.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_dtls_client_cert.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_tls_client_cert.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_relay.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_relay_cert.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_tcp_client.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_tcp_client_c2c_tcp_relay.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_tls_client.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_tls_client_c2c_tcp_relay.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_udp_c2c.sh
%{_datadir}/%{name}/scripts/longtermsecure/secure_udp_client.sh
%dir %{_datadir}/%{name}/scripts/longtermsecuredb
%{_datadir}/%{name}/scripts/longtermsecuredb/secure_relay_with_db_mysql.sh
%{_datadir}/%{name}/scripts/longtermsecuredb/secure_relay_with_db_mysql_ssl.sh
%{_datadir}/%{name}/scripts/longtermsecuredb/secure_relay_with_db_psql.sh
%{_datadir}/%{name}/scripts/longtermsecuredb/secure_relay_with_db_redis.sh
%dir %{_datadir}/%{name}/scripts/restapi
%{_datadir}/%{name}/scripts/restapi/secure_relay_secret.sh
%{_datadir}/%{name}/scripts/restapi/secure_relay_secret_with_db_mysql.sh
%{_datadir}/%{name}/scripts/restapi/secure_relay_secret_with_db_psql.sh
%{_datadir}/%{name}/scripts/restapi/secure_relay_secret_with_db_redis.sh
%{_datadir}/%{name}/scripts/restapi/secure_udp_client_with_secret.sh
%{_datadir}/%{name}/scripts/restapi/shared_secret_maintainer.pl
%dir %{_datadir}/%{name}/scripts/selfloadbalance
%{_datadir}/%{name}/scripts/selfloadbalance/secure_dos_attack.sh
%{_datadir}/%{name}/scripts/selfloadbalance/secure_relay.sh
%dir %{_datadir}/%{name}/scripts/shorttermsecure
%{_datadir}/%{name}/scripts/shorttermsecure/secure_relay_short_term_mech.sh
%{_datadir}/%{name}/scripts/shorttermsecure/secure_tcp_client_c2c_tcp_relay_short_term.sh
%{_datadir}/%{name}/scripts/shorttermsecure/secure_udp_client_short_term.sh
%dir %{_datadir}/%{name}/scripts/mobile
%{_datadir}/%{name}/scripts/mobile/mobile_relay.sh
%{_datadir}/%{name}/scripts/mobile/mobile_dtls_client.sh
%{_datadir}/%{name}/scripts/mobile/mobile_tcp_client.sh
%{_datadir}/%{name}/scripts/mobile/mobile_tls_client_c2c_tcp_relay.sh
%{_datadir}/%{name}/scripts/mobile/mobile_udp_client.sh

%files 		utils
%defattr(-,root,root)
%{_bindir}/turnutils_peer
%{_bindir}/turnutils_stunclient
%{_bindir}/turnutils_uclient
%{_mandir}/man1/turnutils.1.gz
%{_mandir}/man1/turnutils_peer.1.gz
%{_mandir}/man1/turnutils_stunclient.1.gz
%{_mandir}/man1/turnutils_uclient.1.gz
%dir %{_docdir}/%{name}
%{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/README.turnutils
%dir %{_datadir}/%{name}
%dir %{_datadir}/%{name}/etc
%{_datadir}/%{name}/etc/turn_client_cert.pem
%{_datadir}/%{name}/etc/turn_client_pkey.pem

%files		client-libs
%{_docdir}/%{name}/LICENSE
%{_libdir}/libturnclient.a

%files		client-devel
%{_docdir}/%{name}/LICENSE
%dir %{_includedir}/turn
%{_includedir}/turn/ns_turn_defs.h
%dir %{_includedir}/turn/client
%{_includedir}/turn/client/ns_turn_ioaddr.h
%{_includedir}/turn/client/ns_turn_msg_addr.h
%{_includedir}/turn/client/ns_turn_msg_defs.h
%{_includedir}/turn/client/ns_turn_msg.h
%{_includedir}/turn/client/TurnMsgLib.h

%changelog
* Mon Nov 10 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.5.2
* Thu Nov 07 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.5.1
* Sun Oct 31 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.4.6
* Thu Oct 16 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.4.5
* Fri Sep 05 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.4.4
* Mon Sep 01 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.4.3
* Thu Aug 14 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.4.2
* Tue Jul 29 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.4.1
* Fri Jul 18 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.96
* Fri Jul 11 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.95
* Wed Jun 25 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.94
* Tue Jun 03 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.92
* Mon Jun 02 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.91
* Fri May 30 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.9
* Fri May 02 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.8
* Sun Apr 23 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.7
* Sun Apr 13 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.6
* Mon Apr 07 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.5
* Sun Apr 06 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.4
* Fri Apr 04 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.3
* Sun Mar 30 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.2
* Sat Mar 29 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.3.1
* Mon Mar 17 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.912
* Mon Mar 10 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.911
* Sun Mar 09 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.910
* Sun Mar 02 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.9
* Tue Feb 18 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.8
* Wed Feb 12 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.7
* Tue Feb 04 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.6
* Sat Jan 25 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.5
* Fri Jan 24 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.4
* Thu Jan 23 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.3
* Tue Jan 21 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.2.2
* Sat Jan 11 2014 Oleg Moskalenko <mom040267@gmail.com>
  - CPU optimization, added to 3.2.2.1
* Mon Jan 06 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Linux epoll performance improvements, added to 3.2.1.4
* Mon Jan 06 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Telnet client installation added to 3.2.1.3
* Sun Jan 05 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.1.2
* Fri Jan 03 2014 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.1.1
* Thu Dec 26 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.2.1.0
* Wed Dec 25 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.1.6.0
* Mon Dec 23 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.1.5.3
* Fri Dec 20 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.1.5.1
* Thu Dec 19 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.1.4.2
* Sat Dec 14 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Sync to 3.1.3.1
* Wed Dec 11 2013 Oleg Moskalenko <mom040267@gmail.com>
  - OpenSSL installation fixed 3.1.2.3
* Tue Dec 10 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 3.1.2.2
* Mon Dec 09 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 3.1.2.1
* Sun Dec 01 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 3.1.1.0
* Sat Nov 30 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 3.0.2.1.
* Thu Nov 28 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Config file setting fixed: version 3.0.1.4.
* Wed Nov 27 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Config file setting fixed: version 3.0.1.3.
* Mon Nov 25 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 3.0.1.2
* Sun Nov 10 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 3.0.0.0
* Fri Nov 8 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 2.6.7.2
* Thu Nov 7 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 2.6.7.1
* Sun Nov 3 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 2.6.7.0
* Sat Nov 2 2013 Peter Dunkley <peter.dunkley@crocodilertc.net>
  - Added Fedora support
* Thu Oct 31 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 2.6.6.2
* Sun Oct 27 2013 Oleg Moskalenko <mom040267@gmail.com>
  - Updated to version 2.6.6.1
* Sun Oct 27 2013 Peter Dunkley <peter.dunkley@crocodilertc.net>
  - Updated to version 2.6.6.0
* Fri May 3 2013 Peter Dunkley <peter.dunkley@crocodilertc.net>
  - First version

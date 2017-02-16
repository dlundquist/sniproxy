package TestUtils;

use warnings;
use strict;
use POSIX ":sys_wait_h";
use IO::Socket::INET;
require File::Temp;
require Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(start_child reap_children wait_for_type wait_for_port make_config);
our $VERSION = '0.01';

$SIG{CHLD} = \&REAPER;

my %children;

sub REAPER {
    my $stiff;
    while (($stiff = waitpid(-1, &WNOHANG)) > 0) {
        # do something with $stiff if you want
        $children{$stiff}->{'running'} = undef;
        $children{$stiff}->{'exit_code'} = $? >> 8;
        $children{$stiff}->{'signal'} = $? & 127;
        $children{$stiff}->{'core_dumped'} = $? & 128;
    }
    $SIG{CHLD} = \&REAPER; # install *after* calling waitpid
}

# Make several requests through the proxy specifying the host header
sub start_child {
    my $type = shift;
    my $child = shift;
    my @args = @_;

    my $pid = fork();
    if (not defined $pid) {
        die("fork: $!");
    } elsif ($pid == 0) {
        undef $SIG{CHLD};
        $child->(@args);
        # Should not be reached
        exit(99);
    }

    $children{$pid} = {
        type => $type,
        pid => $pid,
        running => 1,
        core_dumped => undef,
        signal => undef,
        exit_core => undef,
    };

    return $pid;
}

sub reap_children {
    while (my @hit_list = grep($children{$_}->{'running'}, keys %children)) {
        kill 15, @hit_list;
        sleep 1;
    }

    # Check that all our children exited cleanly
    my @failures = grep($_->{'exit_code'} != 0 || $_->{'core_dumped'}, values %children);
    if (@failures) {
        print "Test failed.\n";
        foreach (@failures) {
            if ($_->{'core_dumped'}) {
                printf "%s died with signal %d, %s coredump\n", $_->{'type'}, $_->{'signal'}, $_->{'core_dumped'} ? 'with' : 'without';
            } else {
                print "$_->{'type'} failed with exit code $_->{'exit_code'}\n";
            }
        }
        exit 1;
    } else {
        # print "Test passed.\n";
        return;
    }
}

sub wait_for_type($) {
    my $type = shift;

    while (grep($children{$_}->{'running'} && $children{$_}->{'type'} eq $type, keys %children) > 0) {
        sleep 1;
    }
}

sub wait_for_port {
    my %args = @_;
    my $ip = $args{'ip'} || 'localhost';
    my $port = $args{'port'} or die "port required";

    my $delay = 1;
    while ($delay < 60) {
        my $port_open = undef;
        eval {
            my $socket = IO::Socket::INET->new(PeerAddr => $ip,
                                               PeerPort => $port,
                                               Proto => "tcp",
                                               Type => SOCK_STREAM);
            if ($socket && $socket->connected()) {
                $socket->shutdown(2);
                $port_open = 1;
            }
        };

        return 1 if ($port_open);

        sleep($delay);
        $delay *= 2;
    }

    return undef;
}

sub make_config($$) {
    my $proxy_port = shift;
    my $httpd_port = shift;

    my ($fh, $filename) = File::Temp::tempfile();
    my ($unused, $logfile) = File::Temp::tempfile();

    # Write out a test config file
    print $fh <<END;
# Minimal test configuration

listen 127.0.0.1 $proxy_port {
    proto http

    access_log $logfile
}

table {
    localhost 127.0.0.1 $httpd_port
}
END

    close ($fh);

    return $filename;
}

1;

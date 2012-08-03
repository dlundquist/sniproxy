package TestUtils;

use warnings;
use strict;
use POSIX ":sys_wait_h";
require Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(start_child reap_children wait_for_type);

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
        &{$child}(@args);
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
        exit 0;
    }
}

sub wait_for_type($) {
    my $type = shift;

    while (grep($children{$_}->{'running'} && $children{$_}->{'type'} eq $type, keys %children) > 0) {
        sleep 1;
    }
}

1;

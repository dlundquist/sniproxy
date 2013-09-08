package TestHTTPD;

use warnings;
use strict;
require IO::Socket::INET;
require List::Util;
require Exporter;
require Time::HiRes;
our @ISA = qw(Exporter);
our @EXPORT = qw(new);

# This represents the sizes of chunks of our responses
my $responses = [
    [ 20 ],
    [ 20, 18000],
    [ 22 ],
    [ 200 ],
    [ 20, 1, 1, 1, 1, 1, 1, 200 ],
];

sub httpd {
    my $port = shift;
    my $count = 0;

    my $server = IO::Socket::INET->new(Listen    => 10,
                                       Proto     => 'tcp',
                                       LocalAddr => 'localhost',
                                       LocalPort => $port,
                                       ReuseAddr => 1)
        or die $!;

    $SIG{CHLD} = 'IGNORE';

    while(my $client = $server->accept()) {
        $count ++;
        my $pid = fork();
        next if $pid; # Parent
        die "fork: $!" unless defined $pid;

        # Child
        my @chunks = @{$responses->[$count % scalar @{$responses}]};
        my $content_length = 0;
        map { $content_length += length $_ } @chunks;

        while (my $line = $client->getline()) {
            # print "CLIENT: $line";
            last if $line =~ /^GET/;
        }

        print $client "HTTP/1.0 200 OK\r\n";
        print $client "Content-Type: text/plain\r\n";
        print $client "Content-Length: $content_length\r\n";
        print $client "\r\n";

        # Return data in chunks specified in responses
        while (my $length = shift @chunks) {
            print $client 'X' x $length;
            $client->flush();
            Time::HiRes::usleep(200) if @chunks;
        }

        close($client);
        exit;
    } continue {
        # close child sockets
        close($client);
    }
    die "accept(): $!";
}

1;

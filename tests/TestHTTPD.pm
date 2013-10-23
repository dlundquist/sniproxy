package TestHTTPD;

use warnings;
use strict;
require IO::Socket::INET;
require Socket;
require Exporter;
require Time::HiRes;
our @ISA = qw(Exporter);
our @EXPORT = qw(new);
our $VERSION = '0.01';

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

    my $server = IO::Socket::INET->new(Listen    => Socket::SOMAXCONN(),
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
        map { $content_length += $_ } @chunks;

        while (my $line = $client->getline()) {
            # Wait for blank line indicating the end of the request
            last if $line eq "\r\n";
        }

        # Assume a GET request

        print $client "HTTP/1.1 200 OK\r\n";
        print $client "Server: TestHTTPD/$VERSION\r\n";
        print $client "Content-Type: text/plain\r\n";
        print $client "Content-Length: $content_length\r\n";
        print $client "Connection: close\r\n";
        print $client "\r\n";

        # Return data in chunks specified in responses
        while (my $length = shift @chunks) {
            print $client 'X' x $length;
            $client->flush();
            Time::HiRes::usleep(100) if @chunks;
        }

        $client->close();
        exit 0;
    } continue {
        # close child sockets
        $client->close();
    }
    die "accept(): $!";
}

1;

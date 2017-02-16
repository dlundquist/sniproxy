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


my $count = 0;

# This represents the sizes of chunks of our responses
my $responses = [
    [ 20 ],
    [ 20, 18000],
    [ 22 ],
    [ 200 ],
    [ 20, 1, 1, 1, 1, 1, 1, 200 ],
];

sub default_response_generator {
    $count ++;

    return sub($) {
        my $sock = shift;

        my @chunks = @{$responses->[$count % scalar @{$responses}]};
        my $content_length = 0;
        map { $content_length += $_ } @chunks;

        print $sock "HTTP/1.1 200 OK\r\n";
        print $sock "Server: TestHTTPD/$VERSION\r\n";
        print $sock "Content-Type: text/plain\r\n";
        print $sock "Content-Length: $content_length\r\n";
        print $sock "Connection: close\r\n";
        print $sock "\r\n";

        # Return data in chunks specified in responses
        while (my $length = shift @chunks) {
            print $sock 'X' x $length;
            $sock->flush();
            Time::HiRes::usleep(100) if @chunks;
        }
    };
}

sub httpd {
    my %args = @_;
    my $ip = $args{'ip'} || 'localhost';
    my $port = $args{'port'} || 8081;
    my $responder_generator = $args{'generator'} || \&default_response_generator;

    my $server = IO::Socket::INET->new(Listen    => Socket::SOMAXCONN(),
                                       Proto     => 'tcp',
                                       LocalAddr => $ip,
                                       LocalPort => $port,
                                       ReuseAddr => 1)
        or die $!;

    $SIG{CHLD} = 'IGNORE';

    while(my $client = $server->accept()) {
        my $responder = $responder_generator->();

        my $pid = fork();
        next if $pid; # Parent
        die "fork: $!" unless defined $pid;

        # Child
        #
        # Read HTTP request
        while (my $line = $client->getline()) {
            # Wait for blank line indicating the end of the request
            last if $line eq "\r\n";
        }

        # Assume a GET request
        $responder->($client);

        $client->close();
        exit 0;
    } continue {
        # close child sockets
        $client->close();
    }
    die "accept(): $!";
}

1;

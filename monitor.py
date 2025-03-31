import asyncio

UDP_IP = "192.168.5.116"
UDP_PORT = 5005
LOCAL_PORT = 5005

COLOR_RESET = "\033[0m"
COLOR_BLACK = "\033[30m"
COLOR_RED = "\033[31m"
COLOR_GREEN = "\033[32m"
COLOR_YELLOW = "\033[33m"
COLOR_BLUE = "\033[34m"
COLOR_MAGENTA = "\033[35m"
COLOR_CYAN = "\033[36m"
COLOR_WHITE = "\033[37m"
COLOR_GRAY = "\033[90m"
COLOR_BRIGHT_WHITE = "\033[97m"

LOG_VERBOSE = "V"
LOG_DEBUG = "D"
LOG_INFO = "I"
LOG_WARN = "W"
LOG_ERROR = "E"

LOG_COLORS = {
    LOG_VERBOSE: COLOR_GRAY,
    LOG_DEBUG: COLOR_BLUE,
    LOG_INFO: COLOR_GREEN,
    LOG_WARN: COLOR_YELLOW,
    LOG_ERROR: COLOR_RED,
}


def get_log_color(line: str):
    if not line:
        return COLOR_BRIGHT_WHITE
    level = line[0]
    return LOG_COLORS.get(level, COLOR_BRIGHT_WHITE)


class UDPProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport
        print("[INFO] UDP socket ready")

    def datagram_received(self, data, addr):
        msg = data.decode(errors="ignore")
        print(f"{get_log_color(msg)}{msg}", end="")


async def send_pings(transport):
    while True:
        # msg = f"ping {time.time()}"
        transport.sendto("ping".encode(), (UDP_IP, UDP_PORT))
        print(">> sent: ping")
        await asyncio.sleep(10)


async def main():
    loop = asyncio.get_running_loop()
    transport, protocol = await loop.create_datagram_endpoint(
        lambda: UDPProtocol(), local_addr=("0.0.0.0", LOCAL_PORT)
    )

    try:
        await send_pings(transport)
    except asyncio.CancelledError:
        pass
    finally:
        transport.close()


if __name__ == "__main__":
    asyncio.run(main())

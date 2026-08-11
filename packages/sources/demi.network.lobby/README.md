# demi.network.lobby

Optional deterministic lobby/ready/team/map state above `NetworkSession`.
Games declare the package message names in their network contract and inject a
small adapter; the package never opens sockets or changes engine ownership.

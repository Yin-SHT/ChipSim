#include "Routing_XY.h"

RoutingAlgorithmsRegister Routing_XY::routingAlgorithmsRegister("XY", getInstance());

Routing_XY* Routing_XY::routing_XY = 0;

Routing_XY* Routing_XY::getInstance() {
	if (routing_XY == 0)
		routing_XY = new Routing_XY();

	return routing_XY;
}

vector<int> Routing_XY::route(Router* router, const RouteData& routeData) {
	vector<int> directions;
	Coord current;
	Coord destination;

    // If the destination is -1, we are sending to the HBM (west)
    if (routeData.dst_id == -1) {
        directions.push_back(DIRECTION_WEST);
        return directions;
    }

    // Otherwise, we are routing within the mesh
	current = id2Coord(routeData.current_id);
	destination = id2Coord(routeData.dst_id);

	if (destination.x > current.x)
		directions.push_back(DIRECTION_EAST);
	else if (destination.x < current.x)
		directions.push_back(DIRECTION_WEST);
	else if (destination.y > current.y)
		directions.push_back(DIRECTION_SOUTH);
	else
		directions.push_back(DIRECTION_NORTH);

	return directions;
}

var MILLISECONDS_FROM_1970_TO_2000 = 946684800000; /* = Date.UTC(2000, 0, 1, 0, 0, 0, 0) */

var measure_interval = 60000;

function GPS_request() {
	var now_plus_half =
		Date.now()
			- MILLISECONDS_FROM_1970_TO_2000
			+ measure_interval / 2;
	var planned_time =
		now_plus_half
			- now_plus_half % measure_interval
			+ MILLISECONDS_FROM_1970_TO_2000;
	postMessage(planned_time);
}

addEventListener(
	'message',
	function message(event) {
		measure_interval = event.data;
		setTimeout(GPS_request, 0);
		setTimeout(
			function () {
				setInterval(GPS_request, measure_interval);
			},
			(Date.now() - MILLISECONDS_FROM_1970_TO_2000) % measure_interval
		);
	}
);

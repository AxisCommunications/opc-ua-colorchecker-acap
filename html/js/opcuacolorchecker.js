/**
 * Copyright (C) 2023, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

const parambaseurl = '/axis-cgi/param.cgi?action=';
const paramappname = 'Opcuacolorchecker';
const paramgeturl = parambaseurl + 'list&group=' + paramappname + '.';
const paramseturl = parambaseurl + 'update&' + paramappname + '.';
const appbaseurl = '/local/' + paramappname.toLowerCase() + '/';
const getstatusurl = appbaseurl + 'getstatus.cgi';
const getstatusinterval = 1000;
const pickcurrenturl = appbaseurl + 'pickcurrent.cgi';

const Shape = {
	Ellipse: 0,
	Rectangle: 1
}

var center = {
	X: 0,
	Y: 0
};

function setStatus(status) {
	var statustext = document.getElementById('status');
	if (status) {
		statustext.style.color = '#8dc63f';
		statustext.innerHTML = '&checkmark;'
	} else {
		statustext.style.color = '#ff0033';
		statustext.innerHTML = '&#10008;';
	}
}

function updateStatus() {
	$.get(getstatusurl)
		.done(function (data) {
			setStatus(data.status);
			setTimeout(updateStatus, getstatusinterval);
		})
		.fail(function (jqXHR, textStatus, errorThrown) {
			console.log('FAILED to get status. (' + errorThrown + ')');
			setTimeout(updateStatus, getstatusinterval);
		});
}

function drawEllipseMarker(X, Y, color, lineWidth) {
	ctx.beginPath();
	ctx.strokeStyle = color;
	ctx.lineWidth = lineWidth;

	const w = document.getElementById('markerwidthnumbox').value / 2;
	const h = document.getElementById('markerheightnumbox').value / 2;
	const k = 1.2;
	const dxsize = (k * 2 * w) / 2;
	const dysize = (k * 2 * h) / 2;

	ctx.ellipse(X, Y, w, h, 0, 0, 2 * Math.PI);
	ctx.moveTo(X - dxsize, Y);
	ctx.lineTo(X + dxsize, Y);
	ctx.moveTo(X, Y - dysize);
	ctx.lineTo(X, Y + dysize);

	ctx.stroke();
}

function drawRectangleMarker(X, Y, color, lineWidth) {
	ctx.beginPath();
	ctx.strokeStyle = color;
	ctx.lineWidth = lineWidth;

	const w = document.getElementById('markerwidthnumbox').value / 2;
	const h = document.getElementById('markerheightnumbox').value / 2;
	const x1 = X - w;
	const y1 = Y - h;
	const x2 = X + w;
	const y2 = Y + h;
	const k = 0.4;
	const dx1 = X - k * w;
	const dy1 = Y - k * h;
	const dx2 = X + k * w;
	const dy2 = Y + k * h;

	ctx.moveTo(x1, y1);
	ctx.lineTo(x2, y1);
	ctx.lineTo(x2, y2);
	ctx.lineTo(x1, y2);
	ctx.lineTo(x1, y1);

	ctx.moveTo(dx1, Y);
	ctx.lineTo(dx2, Y);
	ctx.moveTo(X, dy2);
	ctx.lineTo(X, dy1);

	ctx.stroke();
}

function drawMarker(X, Y, color, lineWidth) {
	const val = parseInt(document.getElementById('markershape').value, 10);
	switch (val) {
		case Shape.Ellipse:
			drawEllipseMarker(X, Y, color, lineWidth);
			break;
		case Shape.Rectangle:
			drawRectangleMarker(X, Y, color, lineWidth);
			break;
		default:
			throw new Error('Unsupported marker shape value ' + val);
	}
}

function drawCenter() {
	ctx.clearRect(0, 0, draw.width, draw.height);
	drawMarker(center.X, center.Y, '#ffffffaa', 3);
	drawMarker(center.X, center.Y, '#000000', 1);
}

function updateWH(property, newvalue) {
	var slider = document.getElementById(property.toLowerCase() + 'slider');
	var numbox = document.getElementById(property.toLowerCase() + 'numbox');
	slider.value = newvalue;
	numbox.value = newvalue;
	setParam(property, newvalue);
	drawCenter();
}

function updateWidth(newvalue) {
	updateWH('MarkerWidth', newvalue);
	if (document.getElementById('markerlockaspect').checked) {
		updateWH('MarkerHeight', newvalue);
	}

}

function updateHeight(newvalue) {
	updateWH('MarkerHeight', newvalue);
	if (document.getElementById('markerlockaspect').checked) {
		updateWH('MarkerWidth', newvalue);
	}
}

function updateAspect(checked) {
	const preview = document.getElementById('preview');
	const width = preview.width;
	const height = preview.height;

	const mwmax = checked ? Math.min(width, height) : width;
	const mhmax = checked ? mwmax : height;

	document.getElementById('markerwidthnumbox').max = mwmax;
	document.getElementById('markerwidthslider').max = mwmax;
	document.getElementById('markerheightslider').max = mhmax;
	document.getElementById('markerheightslider').max = mhmax;

	if (checked) {
		var hval = document.getElementById('markerheightnumbox').value;
		updateWH('MarkerWidth', hval);
	}
}

function updateShape(newvalue) {
	setParam('MarkerShape', newvalue);
	drawCenter();
}

function trimColorComponent(colorcomponent) {
	if (0 > colorcomponent) {
		return 0;
	}
	if (255 < colorcomponent) {
		return 255
	}
	return colorcomponent;
}

function getEdgeColor(color, diff) {
	const newR = trimColorComponent(Number(color.R) + Number(diff));
	const newG = trimColorComponent(Number(color.G) + Number(diff));
	const newB = trimColorComponent(Number(color.B) + Number(diff));
	return 'rgb(' + newR + ',' + newG + ',' + newB + ')';
}

function getColorString(thecolor) {
	return 'rgb(' + thecolor.R + ',' + thecolor.G + ',' + thecolor.B + ')';
}

function updateTolerance(tolerance, set = true) {
	var slider = document.getElementById('toleranceslider');
	var numbox = document.getElementById('tolerancenumbox');
	slider.value = tolerance;
	numbox.value = tolerance;
	if (set) {
		setParam('Tolerance', tolerance);
	}
	var gradient = document.getElementById('intervalgradient');
	const thecolor = {
		R: document.getElementById('colR').value,
		G: document.getElementById('colG').value,
		B: document.getElementById('colB').value
	};
	gradient.style.background = 'linear-gradient(to right,' + getEdgeColor(thecolor, -tolerance) + ',' + getColorString(thecolor) + ',' + getEdgeColor(thecolor, tolerance) + ')';
}

function updateR(value) {
	setParam('ColorR', value);
	updateTolerance(document.getElementById('tolerancenumbox').value, false);
}

function updateG(value) {
	setParam('ColorG', value);
	updateTolerance(document.getElementById('tolerancenumbox').value, false);
}

function updateB(value) {
	setParam('ColorB', value);
	updateTolerance(document.getElementById('tolerancenumbox').value, false);
}

function getCurrentValue(param) {
	return new Promise(function (resolve, reject) {
		$.get(paramgeturl + param)
			.done(function (data) {
				var value = data.split('=')[1];
				console.log('Got ' + param + ' value ' + value);
				resolve(Number(value));
			})
			.fail(function (data) {
				alert('FAILED to get ' + param);
				reject(data);
			});
	});
}

function setParam(param, value) {
	$.get(paramseturl + param + '=' + value)
		.done(function (data) {
			console.log('Set ' + param + ' to ' + value);
		})
		.fail(function (jqXHR, textStatus, errorThrown) {
			alert('FAILED to set ' + param);
		});
}

async function initWithCurrentValues() {
	try {
		center.X = await getCurrentValue('CenterX');
		center.Y = await getCurrentValue('CenterY');
		var thecolor = {
			R: await getCurrentValue('ColorR'),
			G: await getCurrentValue('ColorG'),
			B: await getCurrentValue('ColorB')
		}
		var markerwidth = await getCurrentValue('MarkerWidth');
		var markerheight = await getCurrentValue('MarkerHeight');
		var markershape = await getCurrentValue('MarkerShape');
		var tolerance = await getCurrentValue('Tolerance');
		var width = await getCurrentValue('Width');
		var height = await getCurrentValue('Height');
	} catch (error) {
		console.error(error);
	}

	var preview = document.getElementById('preview');
	preview.width = preview.style.width = draw.width = width;
	preview.height = preview.style.height = draw.height = height;
	preview.src = '/axis-cgi/mjpg/video.cgi?resolution=' + width + 'x' + height;

	document.getElementById('colR').value = thecolor.R;
	document.getElementById('colG').value = thecolor.G;
	document.getElementById('colB').value = thecolor.B;
	document.getElementById('markerwidthnumbox').value = markerwidth;
	document.getElementById('markerwidthslider').value = markerwidth;
	document.getElementById('markerheightnumbox').value = markerheight;
	document.getElementById('markerheightslider').value = markerheight;
	document.getElementById('markershape').value = markershape;

	const markerlockaspect = (markerwidth == markerheight);
	document.getElementById('markerlockaspect').checked = markerlockaspect;
	updateAspect(markerlockaspect);
	handleCoord(center.X, center.Y, false);
	updateTolerance(tolerance, false);
}

function handleCoord(X, Y, set = true) {
	center.X = X;
	center.Y = Y;
	if (set) {
		setParam('CenterX', X);
		setParam('CenterY', Y);
	}
	ctx.clearRect(0, 0, draw.width, draw.height);
	drawCenter();
}

function handleMouseClick(event) {
	var offsets = document.getElementById('preview').getBoundingClientRect();
	var top = offsets.top;
	var left = offsets.left;
	var X = Math.round(event.clientX - left);
	var Y = Math.round(event.clientY - top);
	handleCoord(X, Y);
}

function captureColor() {
	$.get(pickcurrenturl)
		.done(function (newColor) {
			document.getElementById('colR').value = newColor.R;
			document.getElementById('colG').value = newColor.G;
			document.getElementById('colB').value = newColor.B;
			updateTolerance(document.getElementById('tolerancenumbox').value, false);
		})
		.fail(function (jqXHR, textStatus, errorThrown) {
			alert('Failed to capture color; is the application running?\n' + '(Error msg: ' + errorThrown + ')');
		});
}

var draw = document.getElementById('draw');
var ctx = draw.getContext('2d');
initWithCurrentValues();
document.getElementById('previewcontainer').addEventListener('click', handleMouseClick);
updateStatus();

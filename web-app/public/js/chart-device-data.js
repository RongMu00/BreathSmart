/*
    @Modify this file to display charts for 4 types of air quality sensor data
    @Reverse Geocoding to get real address from current GPS location
    @reference: https://github.com/Azure-Samples/web-apps-node-iot-hub-data-visualization 
*/


/* eslint-disable max-classes-per-file */
/* eslint-disable no-restricted-globals */
/* eslint-disable no-undef */
$(document).ready(() => {
  // if deployed to a site supporting SSL, use wss://
  const protocol = document.location.protocol.startsWith('https') ? 'wss://' : 'ws://';
  const webSocket = new WebSocket(protocol + location.host);

  // A class for holding the last N points of telemetry for a device
  class DeviceData {
    constructor(deviceId) {
      this.deviceId = deviceId;
      this.maxLen = 50;
      this.timeData = new Array(this.maxLen);
      this.lat = null;
      this.lon = null;
      this.address = null;
      this.temperatureData = new Array(this.maxLen);
      this.humidityData = new Array(this.maxLen);
      this.CO2Data = new Array(this.maxLen);
      this.PM25Data = new Array(this.maxLen);
    }

    addData(time, temperature, humidity, CO2, PM25) {
      this.timeData.push(time);
      this.temperatureData.push(temperature);
      this.humidityData.push(humidity || null);
      this.CO2Data.push(CO2 || null);
      this.PM25Data.push(PM25);

      if (this.timeData.length > this.maxLen) {
        this.timeData.shift();
        this.temperatureData.shift();
        this.humidityData.shift();
        this.CO2Data.shift();
        this.PM25Data.shift();
      }
    }

    addGPSData(latitude, longitude, address) {
      this.lat = latitude;
      this.lon = longitude;
      this.address = address;
    }
  }

  // All the devices in the list (those that have been sending telemetry)
  class TrackedDevices {
    constructor() {
      this.devices = [];
    }

    // Find a device based on its Id
    findDevice(deviceId) {
      for (let i = 0; i < this.devices.length; ++i) {
        if (this.devices[i].deviceId === deviceId) {
          return this.devices[i];
        }
      }

      return undefined;
    }

    getDevicesCount() {
      return this.devices.length;
    }
  }

  const trackedDevices = new TrackedDevices();

  const tempChartData = {
    datasets: [
      {
        fill: false,
        label: 'Temperature',
        yAxisID: 'Temperature',
        borderColor: 'rgba(255, 204, 0, 1)',
        pointBoarderColor: 'rgba(255, 204, 0, 1)',
        backgroundColor: 'rgba(255, 204, 0, 0.4)',
        pointHoverBackgroundColor: 'rgba(255, 204, 0, 1)',
        pointHoverBorderColor: 'rgba(255, 204, 0, 1)',
        spanGaps: true,
      }
    ]
  }

  const humidityChartData = {
    datasets: [
      {
        fill: false,
        label: 'Humidity',
        yAxisID: 'Humidity',
        borderColor: 'rgba(24, 120, 240, 1)',
        pointBoarderColor: 'rgba(24, 120, 240, 1)',
        backgroundColor: 'rgba(24, 120, 240, 0.4)',
        pointHoverBackgroundColor: 'rgba(24, 120, 240, 1)',
        pointHoverBorderColor: 'rgba(24, 120, 240, 1)',
        spanGaps: true,
      }
    ]
  }

  const tempChartOptions = {
    scales: {
      yAxes: [{
        id: 'Temperature',
        type: 'linear',
        scaleLabel: {
          labelString: 'Temperature (ºC)',
          display: true,
        },
        position: 'left',
        ticks: {
          suggestedMin: 0,
          suggestedMax: 40,
          beginAtZero: true
        }
      }]
    }
  };

  const humidityChartOptions = {
    scales: {
      yAxes: [{
        id: 'Humidity',
        type: 'linear',
        scaleLabel: {
          labelString: 'Humidity (%)',
          display: true,
        },
        position: 'left',
        ticks: {
          suggestedMin: 30,
          suggestedMax: 70,
          beginAtZero: false
        }
      }]
    }
  };

  const co2ChartData = {
    datasets: [
      {
          fill: false,
          label: 'CO2',
          yAxisID: 'CO2',
          borderColor: 'rgba(100, 200, 50, 1)',
          pointBoarderColor: 'rgba(100, 200, 50, 1)',
          backgroundColor: 'rgba(100, 200, 50, 0.4)',
          pointHoverBackgroundColor: 'rgba(100, 200, 50, 1)',
          pointHoverBorderColor: 'rgba(100, 200, 50, 1)',
          spanGaps: true,
      }
    ]
  };

  const pm25ChartData = {
    datasets: [
      {
          fill: false,
          label: 'PM25',
          yAxisID: 'PM25',
          borderColor: 'rgba(255, 100, 100, 1)',
          pointBoarderColor: 'rgba(255, 100, 100, 1)',
          backgroundColor: 'rgba(255, 100, 100, 0.4)',
          pointHoverBackgroundColor: 'rgba(255, 100, 100, 1)',
          pointHoverBorderColor: 'rgba(255, 100, 100, 1)',
          spanGaps: true,
      }
    ]
  }

    const co2ChartOptions = {
      scales: {
        yAxes: [{
          id: 'CO2',
          type: 'linear',
          scaleLabel: {
            labelString: 'CO2 concentration (ppm)',
            display: true,
          },
          position: 'left',
          ticks: {
            suggestedMin: 400,
            suggestedMax: 1400, // Adjust this as per your requirement
            beginAtZero: false,
          }
        }]
      }
    };

    const pm25ChartOptions = {
      scales: {
        yAxes: [{
          id: 'PM25',
          type: 'linear',
          scaleLabel: {
            labelString: 'PM2.5 (µg/m³)',
            display: true,
          },
          position: 'left',
          ticks: {
            suggestedMin: 0,
            suggestedMax: 10, // Adjust this as per your requirement
            beginAtZero: true
          }
        }]
      }
    };

  const tempCtx = document.getElementById('tempChart').getContext('2d');
  const tempLineChart = new Chart(
    tempCtx,
    {
      type: 'line',
      data: tempChartData,
      options:tempChartOptions,
    }
  );

  const humidityCtx = document.getElementById('humidityChart').getContext('2d');
  const humidityLineChart = new Chart(
    humidityCtx,
    {
      type: 'line',
      data: humidityChartData,
      options: humidityChartOptions,
    }
  );

  const co2Ctx = document.getElementById('co2Chart').getContext('2d');
  const co2LineChart = new Chart(
    co2Ctx,
    {
      type: "line",
      data: co2ChartData,
      options: co2ChartOptions,
    }
  );

  const pm25Ctx = document.getElementById('pm25Chart').getContext('2d');
  const pm25LineChart = new Chart(
    pm25Ctx,
    {
      type: "line",
      data: pm25ChartData,
      options: pm25ChartOptions,
    }
  );

  // Manage a list of devices in the UI, and update which device data the chart is showing
  // based on selection
  let needsAutoSelect = true;
  const deviceCount = document.getElementById('deviceCount');
  const listOfDevices = document.getElementById('listOfDevices');
  function OnSelectionChange() {
    const device = trackedDevices.findDevice(listOfDevices[listOfDevices.selectedIndex].text);

    tempChartData.labels = device.timeData;
    tempChartData.datasets[0].data = device.temperatureData;
    tempLineChart.update();

    humidityChartData.labels = device.timeData;
    humidityChartData.datasets[0].data = device.humidityData;
    humidityLineChart.update();

    co2ChartData.labels = device.timeData;
    co2ChartData.datasets[0].data = device.CO2Data;
    co2LineChart.update();

    pm25ChartData.labels = device.timeData;
    pm25ChartData.datasets[0].data = device.PM25Data;
    pm25LineChart.update();
  }
  listOfDevices.addEventListener('change', OnSelectionChange, false);

  // When a web socket message arrives:
  // 1. Unpack it
  // 2. Validate it has date/time and temperature
  // 3. Find or create a cached device to hold the telemetry data
  // 4. Append the telemetry data
  // 5. Update the chart UI
  webSocket.onmessage = function onMessage(message) {
    try {
      const messageData = JSON.parse(message.data);
      console.log(messageData);

      // time and either temperature or humidity are required
      if (!messageData.MessageDate || (!messageData.IotData.temperature && !messageData.IotData.humidity)) {
        return;
      }

      // if(!messageData.MessageData || (!messageData.IotData.CO2 && !messageData.IotData.PM25)) {
      //   return;
      // }

      // find or add device to list of tracked devices
      const existingDeviceData = trackedDevices.findDevice(messageData.DeviceId);
      const latitude = messageData.IotData.latitude;//+
      const longitude = messageData.IotData.longitude;//+

      // Your Google API key
      const apiKey = 'AIzaSyAXko3W0nirdWf7A6vy-__Emmn_qEGnKgk';//+

      // Fetch address from Google Geocoding API
      fetch(`https://maps.googleapis.com/maps/api/geocode/json?latlng=${latitude},${longitude}&key=${apiKey}`)//+
          .then(response => response.json())
          .then(data => {
              // Get and display address
              if (data.status === "OK") {
                  const address = data.results[0].formatted_address;
                  document.getElementById('address').textContent = 'Current Location: ' + address;
              } else {
                  console.error('Geocoder failed due to: ' + data.status);
              }
          })
          .catch(error => console.error(error));

      if (existingDeviceData) {
        existingDeviceData.addData(messageData.MessageDate, messageData.IotData.temperature, messageData.IotData.humidity, messageData.IotData.CO2, messageData.IotData.PM25);
      } else {
        const newDeviceData = new DeviceData(messageData.DeviceId);
        trackedDevices.devices.push(newDeviceData);
        const numDevices = trackedDevices.getDevicesCount();
        deviceCount.innerText = numDevices === 1 ? `${numDevices} device` : `${numDevices} devices`;
        newDeviceData.addData(messageData.MessageDate, messageData.IotData.temperature, messageData.IotData.humidity, messageData.IotData.CO2, messageData.IotData.PM25);
        // add device to the UI list
        const node = document.createElement('option');
        const nodeText = document.createTextNode(messageData.DeviceId);
        node.appendChild(nodeText);
        listOfDevices.appendChild(node);

        // if this is the first device being discovered, auto-select it
        if (needsAutoSelect) {
          needsAutoSelect = false;
          listOfDevices.selectedIndex = 0;
          OnSelectionChange();
        }
      }

      //myLineChart.update();
      tempLineChart.update();
      humidityLineChart.update();
      // co2pm25LineChart.update();
      co2LineChart.update();
      pm25LineChart.update();

    } catch (err) {
      console.error(err);
    }
  };
});

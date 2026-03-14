## project spec

the goal is to build a flight dashboard app
this will display local weather information


the architecture will consist of

## a python script


this is the driving program that will render the content and push it to the screen uisng the hzeller python bindings
for now just use the hzeller api to render some dummy information like

"Delta Flight"
"Latitude 10"
"Longitude 40"
"Paris Charles"
"Seattle Tacoma"

(it will pull it in from open sky weather api (use dummy data and variables until i input the api keys))

## config
the raspberry pi should broadcast itself using mdns on the local network so the user can change the settings of the program. for now it should just broadcast a static html site

## boot sequence

The pi should always start up both the mdns broadcast and the driver service using a systemd service on the raspberry pi (the user will be nontechnical and unable to handle any failures once the product is shipped)
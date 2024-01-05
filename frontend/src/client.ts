import { Empty, GetAPRequest, PSKRequest, PacketRequest, SSIDList } from './packets_pb';
import { GreeterClient } from './packets_grpc_web_pb';

var client = new GreeterClient('http://localhost:8080');

function getNetworks() {
    console.log("Getting all the deteected networks");
    client.getAllAccessPoints(new Empty(), {}, function(err, response: SSIDList) {
        if (err) {
            console.error("Got err: ", err)
            return;
        }

        let ssidList = response.getNamesList();
        let networkTable = document.getElementById("networks_list");
        networkTable.innerHTML = ''; // Remove everything that was there prev
        for (const ssid of ssidList) {
            let input = document.createElement("input");
            input.type = "radio";

            let th = document.createElement("th");
            th.textContent = ssid; // look into packets.proto:AP for more info
            th.appendChild(input);
            networkTable.append(th)
        }

        console.log("Got access point list: ", ssidList);
    });
};

function getNetworkByName() {
    console.log("Getting a specific network");
    let networkTable = document.getElementById("networks_list");
    let selectedNetwork = ""
    networkTable.childNodes.forEach((row) => {
        let radio = row.lastChild as HTMLInputElement;
        console.log(row.textContent)
        console.log(radio)
        if (radio.checked) selectedNetwork = row.textContent;
    })

    let request = new GetAPRequest();
    request.setSsid(selectedNetwork);
    console.log("Sending request for the network: ", selectedNetwork);

    client.getAccessPoint(request, {}, (err, response) => {
        if (err) {
            console.error("Got err: ", err)
            return;
        }

        let apName = response.getName();
        console.log("apname", apName);
        let apBssid = response.getBssid();
        console.log("apbssid", apBssid);
        let apChannel = response.getChannel();
        console.log("apchan", apChannel);

        let netInfo = document.getElementById("net_info");
        let table = document.createElement('table');

        for (const elem of [apName, apBssid, apChannel]) {
            let row = document.createElement('tr');
            let data = document.createElement('td');
            data.textContent = elem.toString();
            row.appendChild(data)
            table.appendChild(row)
        }

        netInfo.appendChild(table);
    });
}

function tryInputPassword() {
    console.log("Trying to input a password");
    let networkTable = document.getElementById("networks_list");
    let selectedNetwork = ""
    networkTable.childNodes.forEach((row) => {
        let radio = row.lastChild as HTMLInputElement;
        console.log(row.textContent)
        console.log(radio)
        if (radio.checked) selectedNetwork = row.textContent;
    })

    let input = document.getElementById("password_text") as HTMLInputElement;
    let request = new PSKRequest();
    request.setSsid(selectedNetwork);
    request.setPasswd(input.value.trim());

    console.log("Trying to provide a password for the network", selectedNetwork);
    console.log("With password", input.value.trim());

    client.providePassword(request, {}, (err, response) => {
        if (err) {
            console.error("Got err: ", err)
            return;
        }

        if (response.getSuccess) {
            console.log("YAY!")
        } else {
            console.error("BOO!")
        }
    })
}

function tryGetStream() {
    console.log("Trying to get the decrypted stream");
    let networkTable = document.getElementById("networks_list");
    let selectedNetwork = ""
    networkTable.childNodes.forEach((row) => {
        let radio = row.lastChild as HTMLInputElement;
        console.log(row.textContent)
        console.log(radio)
        if (radio.checked) selectedNetwork = row.textContent;
    })

    let request = new PacketRequest();
    request.setSsid(selectedNetwork);

    let stream = client.getDecryptedPackets(request, {});
    stream.on('data', (response) => {
        let from = response.getFrom();
        let to = response.getTo();
        console.log(response.getProtocol(), "from", from.getMacaddress(), from.getIpv4address(), from.getPort(), "to", to.getMacaddress(), to.getIpv4address(), to.getPort());
    })

    stream.on('end', () => {
        console.log("end");
    });
}

document.getElementById('get_networks').addEventListener('click', getNetworks);
document.getElementById('get_ap').addEventListener('click', getNetworkByName);
document.getElementById('put_passwd').addEventListener('click', tryInputPassword);
document.getElementById('get_stream').addEventListener('click', tryGetStream);

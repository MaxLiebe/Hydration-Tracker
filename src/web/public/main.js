var newTime;
var secret;
var leaderboard;

window.onload = async () => {
    (await fetch('leaderboards')).json().then(data => {
        leaderboard = data.default;
        sortLeaderboard();
    });

    const urlParams = new URLSearchParams(window.location.search);
    newTime = urlParams.get('t');
    if(newTime) {
        secret = urlParams.get('s');
        const overlay = document.getElementById('overlay');
        overlay.style.display = 'flex';
        document.getElementById('dialog-time').innerHTML = timeFormatted(newTime);
    }
}

const getRenderedLeaderboardEntryHTML = (index, entry) => {
    let tableRow = document.createElement('tr');

    let number = tableRow.appendChild(document.createElement('td'));
    number.classList.add('number');
    number.innerHTML = ++index;

    let name = tableRow.appendChild(document.createElement('td'));
    name.classList.add('name');
    name.innerHTML = entry.name;

    let time = tableRow.appendChild(document.createElement('td'));
    time.classList.add('time');
    time.innerHTML = timeFormatted(entry.time);

    return tableRow;
}

const timeFormatted = time => {
    let seconds = Math.floor(parseInt(time) / 1000);
    let hundredths = Math.floor((time - seconds * 1000) / 10);
    return `${seconds.toLocaleString('en-US', {minimumIntegerDigits: 2, useGrouping:false})}.${hundredths.toLocaleString('en-US', {minimumIntegerDigits: 2, useGrouping:false})}`;
}

const saveTime = () => {
    let name = document.getElementById('dialog-name').value;
    let entry = {
        name,
        time: newTime
    };

    //honestly don't care about the status of this
    fetch(`savetime?s=${secret}&n=${name}`);
    
    //update the leaderboard locally without refreshing the page
    leaderboard.push(entry);
    sortLeaderboard();
    hideOverlay();
}

const hideOverlay = () => {document.getElementById('overlay').style.display = 'none'};

const sortLeaderboard = () => {
    let leaderboardTable = document.getElementById('leaderboard-table').firstElementChild;
    leaderboardTable.innerHTML = "";

    leaderboard = leaderboard.sort((entry1, entry2) => {
        return entry1.time - entry2.time;
    });

    for(let i = 0; i < leaderboard.length; i++) {
        leaderboardTable.appendChild(getRenderedLeaderboardEntryHTML(i, leaderboard[i]));
    }
}
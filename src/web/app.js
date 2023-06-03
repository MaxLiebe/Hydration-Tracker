const path = require('path');
const express = require('express')
const app = express()
const port = 3000

app.get('/leaderboards', (req, res) => {
  res.header("Content-Type",'application/json');
  res.sendFile(path.join(__dirname, 'leaderboards_example.json'));
});

app.use(express.static('public'))

app.listen(port, () => {
  console.log(`Example app listening on port ${port}`)
})
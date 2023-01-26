const stringifyNice = (data) => JSON.stringify(data, null, 2);
const templates = Object.freeze({
  elements: {
    fileName: "elements.json",
    value: stringifyNice([
      {
        name: "pin",
        type: "ACInput",
        value: "1234",
        label: "PIN code for Bitwatch wallet",
        pattern: "",
        placeholder: "1234",
        style: "",
        apply: "number",
      },
      {
        name: "password",
        type: "ACInput",
        value: "ToTheMoon1",
        label: "Password for Bitwatch AP WiFi",
        pattern: "",
        placeholder: "WIFI password",
        style: "",
        apply: "password",
      },
      {
        name: "seedphrase",
        type: "ACInput",
        value: "",
        label: "Wallet Seed Phrase",
        pattern: "",
        placeholder:
          "word1 word2 word3 word4 word5 word6 word7 word8 word9 word10 word11 word12",
        style: "",
        apply: "text",
      },
    ]),
  },
});

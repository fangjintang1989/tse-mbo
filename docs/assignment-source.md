# Assignment Source

Raw text extracted from `Take home coding assignment.docx`.

Take home coding assignment

You are asked to finish the below assignment. You are welcome to use AI tools to help finishing it on time. But please kindly provide us with all the details, namely which AI model you used during this assignment, if any. We strongly encourage you to finish it in no more than one week’s time. Please let us know, if you need another week to finish it.

The assignment is developing a historical PCAP data processing application, which handles Tokyo Stock Exchange (TSE)’s Flex Full MBO feed. Exchange protocol specification is also attached. You are required to finish the project, in C++. In your README file, please share with us all the information that we should know, including but not limited to:

which OS and version you use,

which compiler and version you use,

how to compile your code,

how to run your program,

anything else that you believe we should know.

There are a few items we need as output from your program (you may find the attached JSON file helpful, as we define stocks as those security type is 1-4, you can also find ticksize table and lot size in the same file. Other information required for finishing this project should be available on TSE website: https://www.jpx.co.jp/english, as public information).

Your code needs to process UDP packets stored in multiple PCAP files, process them and maintain a proper orderbook

For the given PCAP samples, please provide the last calculated indicative open auction match price and quantity/volume, for all stocks, in a csv file, in the format of “symbol, iap, iav”

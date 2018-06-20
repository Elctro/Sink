FROM microsoft/aspnetcore

ENV RESOLVER_ID -

RUN mkdir /app
WORKDIR /app

COPY /Kres.Man/*.csproj ./
RUN dotnet restore

RUN dotnet new global.json

COPY /Kres.Man/ ./
COPY /Kres.Man/startup.sh /usr/local/bin/startup.sh 
RUN chmod +x /usr/local/bin/startup.sh
CMD /usr/local/bin/startup.sh

RUN dotnet publish -c Release -o out

COPY /out/ .
ENTRYPOINT ["dotnet", "Kres.Man.dll"]

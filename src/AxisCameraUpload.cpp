//
// AxisCameraUpload.cpp
//
// A simple HTTP server that accepts image uploads from an Axis network camera.
//
// SPDX-License-Identifier: MIT
//


#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/NumberFormatter.h"
#include "Poco/StreamCopier.h"
#include "Poco/NullStream.h"
#include "Poco/Exception.h"
#include "Poco/LocalDateTime.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/FileStream.h"
#include "Poco/Path.h"
#include "Poco/File.h"
#include "Poco/URI.h"
#include <sstream>
#include <iostream>


using namespace std::string_literals;


class ImageUploadRequestHandler: public Poco::Net::HTTPRequestHandler
{
public:
	void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
	{
		auto& app = Poco::Util::Application::instance();
		const auto& config = app.config();

		try
		{
			if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_POST)
			{
				if (authorize(request, config.getString("upload.token"s, ""s)))
				{
					if (request.getContentType() == "image/jpeg")
					{
						std::string path = storeImage(request, config.getString("upload.path"s, Poco::Path::current()));
						app.logger().information("Image stored to '%s'."s, path);
						return sendResponse(request, Poco::Net::HTTPResponse::HTTP_OK, "Image accepted"s);
					}
					else
					{
						app.logger().warning("Invalid or missing content type '%s' for request from %s: %s %s"s, request.getContentType(), request.clientAddress().toString(), request.getMethod(), request.getURI());
						ignoreContent(request);
						return sendResponse(request, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "Unexpected content type"s);
					}
				}
				else
				{
					app.logger().warning("Invalid or missing token for request from %s: %s %s"s, request.clientAddress().toString(), request.getMethod(), request.getURI());
					ignoreContent(request);
					return sendResponse(request, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "Missing or invalid upload token"s);
				}
			}
			else if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_GET)
			{
				return sendResponse(request, Poco::Net::HTTPResponse::HTTP_OK, "Image upload server ready"s);
			}
			else if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_HEAD)
			{
				response.send();
				return;
			}
			else
			{
				return sendResponse(request, Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED, "Request method not allowed"s);
			}
		}
		catch (Poco::Exception& exc)
		{
			app.logger().log(exc);
			if (!response.sent())
			{
				sendResponse(request, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "error uploading file");
			}
		}
	}

	bool authorize(Poco::Net::HTTPServerRequest& request, const std::string& token)
	{
		Poco::URI uri(request.getURI());
		Poco::Net::HTMLForm params;
		params.read(uri.getRawQuery());
		return params.get("token"s, ""s) == token;
	}

	std::string storeImage(Poco::Net::HTTPServerRequest& request, const std::string& uploadPath)
	{
		Poco::Path p(uploadPath);
		p.makeDirectory();
		p.pushDirectory(uploadSite(request));
		p.pushDirectory(uploadCamera(request));

		Poco::LocalDateTime now;
		p.pushDirectory(Poco::NumberFormatter::format(now.year()));
		p.pushDirectory(Poco::NumberFormatter::format0(now.month(), 2));
		p.pushDirectory(Poco::NumberFormatter::format0(now.day(), 2));
		p.pushDirectory(Poco::NumberFormatter::format0(now.hour(), 2));

		Poco::File dir(p.toString());
		dir.createDirectories();

		p.setFileName(Poco::DateTimeFormatter::format(now, "%Y%m%d-%H%M%S-%F.jpg"s));

		Poco::FileOutputStream fileStream(p.toString());
		Poco::StreamCopier::copyStream(request.stream(), fileStream);

		return p.toString();
	}

	std::string uploadSite(const Poco::Net::HTTPServerRequest& request) const
	{
		Poco::URI uri(request.getURI());
		Poco::Path p(uri.getPath(), Poco::Path::PATH_UNIX);
		p.makeDirectory();
		if (p.depth() >= 2)
		{
			return p[1];
		}
		else
		{
			return "defaultSite";
		}
	}

	std::string uploadCamera(const Poco::Net::HTTPServerRequest& request) const
	{
		Poco::Path p(request.getURI(), Poco::Path::PATH_UNIX);
		p.makeDirectory();
		if (p.depth() >= 3)
		{
			return p[2];
		}
		else
		{
			return "defaultCamera";
		}
	}

	void ignoreContent(Poco::Net::HTTPServerRequest& request)
	{
		Poco::NullOutputStream nullStream;
		Poco::StreamCopier::copyStream(request.stream(), nullStream);
	}

	static void sendResponse(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPResponse::HTTPStatus status, const std::string& message)
	{
		request.response().setContentType("text/html"s);
		request.response().setStatusAndReason(status);

		std::string html("<!DOCTYPE html>\n<html><head><title>");
		html += Poco::NumberFormatter::format(static_cast<int>(status));
		html += " - ";
		html += request.response().getReasonForStatus(status);
		html += "</title></head><body><header><h1>"s;
		html += Poco::NumberFormatter::format(static_cast<int>(status));
		html += " - ";
		html += request.response().getReasonForStatus(status);
		html += "</h1></header><section><p>"s;
		html += Poco::Net::htmlize(message);
		html += "</p></section>"s;
		html += "</body></html>"s;
		request.response().sendBuffer(html.data(), html.size());
	}
};


class ImageUploadRequestHandlerFactory: public Poco::Net::HTTPRequestHandlerFactory
{
public:
	Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest& request)
	{
		auto& app = Poco::Util::Application::instance();
		const auto& config = app.config();

		app.logger().information("Request from %s: %s %s"s, request.clientAddress().toString(), request.getMethod(), request.getURI());
		if (app.logger().debug())
		{
			std::ostringstream sstr;
			request.write(sstr);
			app.logger().debug("Request details: %s"s, sstr.str());
		}

		return new ImageUploadRequestHandler;
	}
};


class ImageUploadServer: public Poco::Util::ServerApplication
{
protected:
	void initialize(Poco::Util::Application& self)
	{
		loadConfiguration(); // load default configuration files, if present
		Poco::Util::ServerApplication::initialize(self);
	}

	void uninitialize()
	{
		Poco::Util::ServerApplication::uninitialize();
	}

	void defineOptions(Poco::Util::OptionSet& options)
	{
		Poco::Util::ServerApplication::defineOptions(options);

		options.addOption(
			Poco::Util::Option("help", "h", "Display help information on command line arguments.")
				.required(false)
				.repeatable(false)
				.callback(Poco::Util::OptionCallback<ImageUploadServer>(this, &ImageUploadServer::handleHelp)));

		options.addOption(
			Poco::Util::Option("config-file", "c", "Load configuration data from a file.")
				.required(false)
				.repeatable(true)
				.argument("file")
				.callback(Poco::Util::OptionCallback<ImageUploadServer>(this, &ImageUploadServer::handleConfig)));
	}

	void handleHelp(const std::string& name, const std::string& value)
	{
		_showHelp = true;
		displayHelp();
		stopOptionsProcessing();
	}

	void handleConfig(const std::string& name, const std::string& value)
	{
		loadConfiguration(value);
	}

	void displayHelp()
	{
		Poco::Util::HelpFormatter helpFormatter(options());
		helpFormatter.setCommand(commandName());
		helpFormatter.setUsage("OPTIONS");
		helpFormatter.setHeader("Image upload server for Axis network cameras.");
		helpFormatter.format(std::cout);
	}

	int main(const std::vector<std::string>& args)
	{
		if (!_showHelp)
		{
			Poco::UInt16 port = static_cast<Poco::UInt16>(config().getInt("http.port"s, 9980));
			Poco::Net::ServerSocket svs(port);
			Poco::Net::HTTPServer srv(new ImageUploadRequestHandlerFactory, svs, new Poco::Net::HTTPServerParams);
			srv.start();
			waitForTerminationRequest();
			srv.stop();
		}
		return Application::EXIT_OK;
	}

private:
	bool _showHelp = false;
};


POCO_SERVER_MAIN(ImageUploadServer)

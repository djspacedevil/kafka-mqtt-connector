#include "MqttConnector.h"

namespace kb{
    namespace mqtt{
        MqttConnector * MqttConnector::GetInstance(const char *id, bool clean_session)
        {
            static MqttConnector mqtt(id,clean_session);
            return &mqtt;
        }

        void MqttConnector::SetQos(std::uint8_t p_qos)
        {
            m_qos = p_qos;
        }

        void MqttConnector::SubscribeToTopics()
        {
            for(auto & elem: m_topics)
            {
                this->subscribe(0,elem.first.c_str(),m_qos);
            }
        }
        void MqttConnector::UnsubscribeToTopics()
        {
            for(auto & elem: m_topics)
            {
                this->unsubscribe(0,elem.first.c_str());
            }
        }

        void MqttConnector::SetTopicsToSubscribeTo(const std::multimap<std::string,std::string>& p_topics)
        {
            if(m_connected)
            {
                if(m_topics.size()>0)
                {
                    UnsubscribeToTopics();
                }
            }
            m_topics=std::move(p_topics);
            if(m_connected)
            {
                SubscribeToTopics();
            }
        }

        // names in the functions commented to prevent unused parameter warning
        void MqttConnector::on_connect(int /*rc*/) 
        {
            m_connected=true;
#ifdef DEBUG
            std::cout << "Connected to server" << std::endl;
#endif
            SubscribeToTopics();
        }
        void MqttConnector::on_disconnect(int /*rc*/) 
        {
            m_connected=false;
#ifdef DEBUG
            std::cout << "Disconnected from server" << std::endl;
#endif
        }
        void MqttConnector::on_publish(int /*mid*/) 
        {
#ifdef DEBUG
            std::cout << "Message successfully published" <<std::endl;
#endif
        }

        void MqttConnector::on_message(const struct mosquitto_message * p_message) 
        {
            std::string message((char *)p_message->payload,p_message->payloadlen);
#ifdef DEBUG
            std::cout << "Got message from topic: " << p_message->topic <<std::endl;
            std::cout << "message: " << (char *)p_message->payload << std::endl;
#endif
            std::string topic{p_message->topic};
            bool flag{false};
            for(auto elem : m_topics)
            {
                mosqpp::topic_matches_sub(elem.first.c_str(),topic.c_str(),&flag);
                if(flag)
                {
                    SendToKafka(elem.second.c_str(),topic,(char *)p_message->payload);
                    flag=false;
                }
            }
        }

        void MqttConnector::on_subscribe(int /*mid*/, int /*qos_count*/, const int * /*granted_qos*/) 
        {
#ifdef DEBUG
            std::cout << "Subscription successful" << std::endl;
#endif
        }
        void MqttConnector::on_unsubscribe(int /*mid*/) 
        {
#ifdef DEBUG
            std::cout << "Unsubscription successful" << std::endl;
#endif
        }

        std::string MqttConnector::GetCurrentISO8601TimeUTC()
        {
            auto now = std::chrono::system_clock::now();
            auto itt = std::chrono::system_clock::to_time_t(now);
            std::ostringstream ss;
            ss << std::put_time(gmtime(&itt), "%FT%TZ");
            return ss.str();   
        }

        void MqttConnector::on_log(int p_level, const char * p_log)
        {
#ifdef DEBUG
            std::cout<< GetCurrentISO8601TimeUTC()<<" [Log Level "<<p_level<<"] " << p_log << std::endl;
#endif
        }
        void MqttConnector::on_error() 
        {
#ifdef DEBUG
            std::cout << "An error happend" <<std::endl;
#endif
        }

        void MqttConnector::SetTraceContextFlag(bool p_flag)
        {
            m_useTraceContext=p_flag;
        }
        
        void MqttConnector::SetProducer(std::shared_ptr<kafka::clients::producer::KafkaProducer> p_producer)
        {
            m_producer = p_producer;
        }

        std::optional<std::vector<kafka::Header>> MqttConnector::AddTraceContext(const std::string& p_msg)
        {
            std::vector<kafka::Header> headers;
            headers.reserve(2);
            std::regex regex(m_regexString);
            auto matchBegin = std::sregex_iterator(p_msg.begin(), p_msg.end(), regex);
            auto matchEnd = std::sregex_iterator();
            for (std::sregex_iterator i = matchBegin; i != matchEnd; ++i) {
                std::smatch match = *i;
                if(match.size()>2)
                {
                    kafka::Header h;
                    h.key = match[1].str();
                    std::string * msg{new std::string(match[2].str())};
                    h.value = kafka::ConstBuffer(msg->c_str(),msg->size());
                    headers.push_back(h);
                }
            }
            //Check if there is content
            if(headers.size()>=1)
            {
                return headers;
            }
            return {};
        }

        void MqttConnector::SendToKafka(const std::string & p_topic,const std::string& p_header, const std::string p_message)
        {
            if(m_producer==nullptr)
            {
                std::stringstream ss;
                ss << "You must initialize the Kafka Producer first! File: " << __FILE__ << " Line: " << __LINE__;
                throw std::invalid_argument(ss.str());
            }
            kafka::Topic topic(p_topic.c_str());
            auto record = kafka::clients::producer::ProducerRecord(topic,kafka::Key(p_header.c_str(),p_header.size()),kafka::Value(p_message.c_str(), p_message.size()));
            if(this->m_useTraceContext)
            {
                auto newHeader = this->AddTraceContext(p_message);
                if(newHeader.has_value())
                {
                    record.headers() = newHeader.value();
                }
            }
            // Send the message
            m_producer->send(record,
                // The delivery report handler
                [](const kafka::clients::producer::RecordMetadata& metadata, const kafka::Error& error) {
                    if (!error) {
#ifdef DEBUG
                        std::cout << "% Message delivered: " << metadata.toString() << std::endl;
#endif
                    } else {
                        std::cerr << "% Message delivery failed: " << error.message() << std::endl;
                    }
                },
                // The memory block given by record.value() would be copied
                kafka::clients::producer::KafkaProducer::SendOption::ToCopyRecordValue);
        }

        MqttConnector::MqttConnector(const char *id, bool clean_session):mosqpp::mosquittopp(id,clean_session)
        {
            mosqpp::lib_init();
        }
    }
}